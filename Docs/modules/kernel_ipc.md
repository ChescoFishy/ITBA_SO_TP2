# Kernel IPC Module (Inter-Process Communication)

## Overview (¿Qué es?)
El módulo [[kernel_ipc.md|IPC]] (Comunicación entre Procesos) provee los mecanismos necesarios para que procesos independientes o relacionados puedan intercambiar flujos de datos y coordinar su ejecución de forma segura. En nuestro sistema operativo, se compone de dos primitivas principales: **Semáforos Nombrados** para la sincronización y exclusión mutua, y **Pipes Unidireccionales** (nombrados o anónimos) para la transferencia de flujos de bytes.

## Functionality (¿Qué hace?)
### 1. Semáforos Nombrados
- Permiten la sincronización y protección de secciones críticas entre procesos no relacionados genéticamente mediante identificadores alfanuméricos.
- Ofrecen operaciones atómicas de espera (`sem_wait`) y señalización (`sem_post`).
- Manejan colas de espera por semáforo para evitar el uso de *busy waiting* (espera activa), bloqueando y suspendiendo procesos de la cola de ejecución.
- Garantizan exclusión mutua interna utilizando spinlocks atómicos por hardware (`lock xchg`).

### 2. Pipes (Tuberías)
- Proveen canales unidireccionales de transferencia de bytes con semántica FIFO.
- Ofrecen abstracción transparente de descriptores de archivos, integrados directamente con las lecturas (`sys_read`) y escrituras (`sys_write`) del sistema.
- Bloquean automáticamente al proceso lector si la tubería está vacía, y al escritor si el buffer interno está lleno.
- Admiten pipes anónimos (para redirección de comandos en la shell `p1 | p2`) y pipes nombrados (para procesos independientes).
- Gestionan de forma segura el fin de archivo (EOF) si se cierran los extremos de escritura, y el canal roto (Broken Pipe) si se cierran los extremos de lectura.

## Internal Mechanics (¿Cómo funciona?)
### 1. Sincronización: Semáforos
- **Spinlock de Control:** Toda mutación sobre la tabla de semáforos se serializa utilizando el cerrojo global `sem_lock` y las funciones ensamblador `acquire` y `release` de `libasm.asm`. `acquire` realiza un ciclo ocupado ejecutando `xchg rax, [rdi]` (donde `rax` es 1), garantizando atomicidad mediante el bloqueo del bus de memoria por hardware.
- **Mecanismo de Bloqueo (Entrega directa del token):** Al ejecutar `sem_wait`:
  - Si `s->value > 0`, se decrementa el valor y se otorga el recurso de inmediato.
  - Si `s->value == 0`, el proceso actual se agrega a la cola circular `s->wait_queue[MAX_PROCESSES]`, se marca su estado como `PROCESS_BLOCKED` mediante `process_block()`, se libera `sem_lock` y se cede el procesador llamando a `kernel_yield()` (`int 0x81`).
  - Al despertarse ya tiene el token y retorna sin re-chequear `value`. Si lo despertó algo distinto de `sem_post` (`sys_unblock` o el comando `block`), sigue en la cola y vuelve a bloquearse.
- **Señalización:** Al ejecutar `sem_post`, si hay procesos en la cola (`s->wait_count > 0`) se le entrega el token al primero (`SEM_WAIT_GRANTED`) y se lo pasa a listos con `process_unblock()`, sin incrementar `s->value`. Si la cola está vacía, se incrementa `s->value`. Como un proceso que llega después no puede adelantarse a los de la cola, el orden es FIFO y no hay inanición.
- **Procesos que mueren esperando:** `sem_cancel_wait` retira de la cola al proceso matado y, si ya tenía un token entregado que no llegó a usar, lo devuelve. El token de un proceso matado dentro de su sección crítica no se recupera.
- **Destrucción:** Cuando `sem_close` destruye el semáforo, los procesos que seguían en la cola se despiertan y su `sem_wait` retorna `-1`.
- **Despertar sin token:** `sem_wake_all` despierta a toda la cola sin cambiar `value`, y cada `sem_wait` retorna `-1`. Es de uso interno del kernel: avisa un cambio ajeno al contador, y quien despierta debe revisar su condición.

### 2. Comunicación: Pipes
- **Buffer Circular y Semáforos de Estado:** Cada `Pipe` gestiona un buffer circular de caracteres de tamaño `PIPE_BUF_SIZE = 4096`. El control de bloqueo por falta de espacio o falta de datos se realiza asociando internamente tres semáforos nombrados únicos al inicializar la tubería:
  - `data_sem` (nombre `__pipe_d_X`): Inicializado en 0. Cuenta la cantidad de bytes legibles disponibles en el buffer.
  - `space_sem` (nombre `__pipe_s_X`): Inicializado en `PIPE_BUF_SIZE`. Cuenta el espacio libre disponible para escribir.
  - `mutex_sem` (nombre `__pipe_m_X`): Inicializado en 1. Garantiza exclusión mutua para la modificación de los índices de lectura/escritura (`head`, `tail`) del buffer circular.
- **Lectura/Escritura Bloqueante:**
  - `pipe_write` hace `sem_wait(space_sem)` para asegurar que haya espacio libre, luego toma exclusión con `mutex_sem`, escribe el byte en `buffer[tail]`, incrementa `tail` de forma circular, libera `mutex_sem` y hace `sem_post(data_sem)` para notificar al lector de la llegada de datos.
  - `pipe_read` funciona de forma análoga haciendo `sem_wait(data_sem)` y `sem_post(space_sem)`.
- **Cierre y EOF:** 
  - Al cerrar un pipe de lectura (`pipe_close` de un lector), si la cantidad de lectores llega a 0, `sem_wake_all(space_sem)` despierta a los escritores bloqueados, que retornan `-1` indicando error de tubería rota (*broken pipe*).
  - Al cerrar un pipe de escritura, si los escritores llegan a 0, `sem_wake_all(data_sem)` despierta a los lectores bloqueados, que retornan `0` indicando fin de archivo (EOF) cuando ya no quedan datos.
  - Los semáforos no reciben tokens extra al cerrar: `data_sem` siempre cuenta los bytes almacenados y `space_sem` los lugares libres, aunque otro proceso reabra el pipe.
- **Pipes nombrados:** `pipe_open(nombre, modo)` abre un solo extremo y crea el pipe si no existe. `sys_pipe_open` llama además a `pipe_wait_peer`, que bloquea hasta que el otro extremo esté abierto, como un FIFO de POSIX. La espera termina si hay un extremo abierto o si hubo una apertura nueva (`read_opens`/`write_opens`), porque el otro proceso pudo abrir, escribir y cerrar antes de que este volviera a correr. El pipe se destruye al cerrarse su último extremo.
- **Extremos por proceso:** `sys_pipe` y `sys_pipe_open` registran cada fd en `PCB.pipe_fds`. Solo se pueden leer, escribir o cerrar los extremos propios (además de stdin/stdout, que se leen y escriben pero no se cierran con `sys_pipe_close`), y el kernel cierra lo que quede abierto cuando el proceso termina o lo matan.

## Key Files & Structs (Archivos y Estructuras Clave)
* **Archivos principales:**
  - `Kernel/c/ipc/semaphore.c`: Administración de la tabla de semáforos, encolamiento de procesos bloqueados y primitivas de sincronización.
  - `Kernel/include/ipc/semaphore.h`: Interfaz del módulo de semáforos.
  - `Kernel/c/ipc/pipe.c`: Manejo de buffers de tuberías, inicialización de semáforos de estado, lógica de cierre y direccionamiento de pipes.
  - `Kernel/include/ipc/pipe.h`: Definición de constantes de pipes (`PIPE_FD_READ_BASE = 100`, `PIPE_FD_WRITE_BASE = 200`).
  - `Kernel/asm/libasm.asm`: Código del spinlock atómico de control (`acquire`/`release`).
* **Estructuras de datos:**
  - `struct Semaphore`:
    ```c
    typedef struct {
        char     name[SEM_NAME_LEN];        /* Identificador alfanumérico */
        int64_t  value;                     /* Contador de tokens */
        uint64_t wait_queue[MAX_PROCESSES]; /* Cola de PIDs bloqueados */
        int      wait_head, wait_tail, wait_count;
        int      open_count;                /* Cantidad de referencias abiertas */
    } Semaphore;
    ```
  - `struct Pipe`:
    ```c
    typedef struct {
        int      in_use;
        char     buffer[PIPE_BUF_SIZE];
        int      head, tail;                /* Índices circulares */
        int      count;                     /* Bytes almacenados */
        int      readers, writers;          /* Cantidad de descriptores activos */
        uint64_t read_opens, write_opens;   /* Veces que se abrió cada extremo */
        char     data_sem[16];              /* Semáforo de bytes disponibles */
        char     space_sem[16];             /* Semáforo de espacio libre */
        char     mutex_sem[16];             /* Exclusión mutua del buffer */
        char     name[PIPE_NAME_LEN];       /* Nombre si es pipe nombrado */
    } Pipe;
    ```
* **Funciones fundamentales:**
  - `acquire(uint64_t *lock)`: Adquiere de manera atómica el spinlock de control mediante `xchg`.
  - `pipe_create(int fds[2])`: Inicializa un canal de comunicación anónimo y devuelve los FDs asignados.
  - `pipe_open(const char *name, int mode)`: Abre un extremo de un pipe nombrado y devuelve su FD.
  - `pipe_wait_peer(int fd)`: Bloquea hasta que el extremo opuesto esté abierto.

## System Calls Relacionadas
- **`sys_sem_open` (Syscall 29) / `sys_sem_close` (Syscall 32):** Crean/abren y cierran semáforos.
- **`sys_sem_wait` (Syscall 30) / `sys_sem_post` (Syscall 31):** Primitivas de decremento/incremento de semáforo.
- **`sys_pipe` (Syscall 33) / `sys_pipe_close` (Syscall 34):** Creación y cierre de tuberías anónimas.
- **`sys_pipe_open` (Syscall 36):** Abre un extremo de una tubería nombrada (`PIPE_READ` o `PIPE_WRITE`), creándola si no existe, y bloquea hasta que el otro extremo esté abierto. Devuelve el fd, o `-1` si el nombre está vacío o supera los 15 caracteres, el modo no es válido o no quedan pipes.
- **`sys_read` (Syscall 3) / `sys_write` (Syscall 4):** Aceptan stdin/stdout o un extremo abierto por el proceso. Devuelven `-1` sobre un extremo que el proceso no tiene abierto.

## Comments and Limitations (Comentarios y Limitaciones)
- **Límites Fijos:** La tabla de semáforos está limitada a `MAX_SEMS = 32` y la de pipes a `MAX_PIPES = 16`. Exceder este límite provocará fallos de apertura/creación en las llamadas al sistema. Como cada pipe usa tres semáforos, entran como mucho 10 pipes a la vez. Un proceso puede tener abiertos hasta `MAX_PIPE_FDS = 8` extremos.
- **Capacidad Estática:** El tamaño de buffer de cada pipe es rígido (`PIPE_BUF_SIZE = 4096`).
- **Mapeo de Descriptores:** Los fds virtuales de los procesos se corresponden físicamente con índices estáticos en la tabla global: `100 + idx` para lectura y `200 + idx` para escritura.
