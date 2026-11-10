# TP2 Sistemas Operativos (ITBA)

Kernel bare-metal x86-64 que corre directamente sobre QEMU o hardware real, sin sistema operativo subyacente. Implementa memoria, procesos, scheduling y syscalls desde cero.

## Integrantes del Grupo

| Nombre                    | Legajo    |         Mail                     |
|---------------------------|-----------|----------------------------------|
| Julia Petti Pagadizabal   | 62453     | jpettipagadizabal@itba.edu.ar    |
| Mateo Reino               | 61307     | mreino@itba.edu.ar               |
| Francesco Vega Scarabino  | 65199     | fvegascarabino@itba.edu.ar       |

## Requisitos

- Docker (único requisito en el host)
- QEMU (`qemu-system-x86_64`) para ejecutar la imagen

---

## Primer uso: crear el contenedor

```bash
./create.sh
```

Descarga la imagen `agodio/itba-so-multiarch:3.1` y crea el contenedor `TP_SO_2` con el directorio actual montado en `/root`. Solo es necesario hacerlo una vez.

---

## Compilar

```bash
./compile.sh           # compila con First-Fit (default)
./compile.sh vbox      # ídem, genera imagen VirtualBox (.vmdk)
```

### Selección del memory manager

El kernel soporta dos implementaciones de memoria, seleccionables en tiempo de compilación mediante la variable de entorno `MM`:

| Comando | Memory Manager |
|---------|---------------|
| `./compile.sh` | First-Fit (default) |
| `MM=FF ./compile.sh` | First-Fit (explícito) |
| `MM=BUDDY ./compile.sh` | Buddy System |

```bash
# Buddy System + imagen QEMU
MM=BUDDY ./compile.sh

# Buddy System + imagen VirtualBox
MM=BUDDY ./compile.sh vbox
```

> **First-Fit**: lista enlazada de bloques libres; busca el primer bloque que ajuste.
> **Buddy System**: bloques de potencia de 2; divide y fusiona en pares (`buddies`).

La selección es **exclusiva en compilación**: una imagen usa un único MM. Para comparar ambos hay que compilar y correr por separado.

### Reglas `make` (dentro del contenedor)

`./compile.sh` y `MM=BUDDY ./compile.sh` son wrappers que ejecutan `make` dentro del contenedor `TP_SO_2`. Si ya estás dentro del contenedor, las reglas equivalentes son:

| Regla | Memory Manager |
|-------|---------------|
| `make` / `make all` | First-Fit (default) |
| `make buddy` | Buddy System (equivale a `make MM=BUDDY all`) |

### Reglas `make` (en el host)

Estas reglas se ejecutan fuera del contenedor. `make`, `make all` y `make buddy` quedan reservadas para compilar dentro de la imagen.

| Regla | Acción |
|-------|--------|
| `make docker-pull` | Descarga la imagen `agodio/itba-so-multiarch:3.1` |
| `make docker-create` | Descarga la imagen, crea y arranca el contenedor `TP_SO_2` (equivale a `./create.sh`) |
| `make docker-compile` | Compila con First-Fit dentro del contenedor (equivale a `MM=FF ./compile.sh`) |
| `make docker-compile-buddy` | Compila con Buddy System dentro del contenedor (equivale a `MM=BUDDY ./compile.sh`) |
| `make qemu` | Arranca la imagen compilada (equivale a `./run.sh`) |

`docker-compile`, `docker-compile-buddy` y `qemu` aceptan `TARGET=vbox` o `TARGET=usb`; por defecto usan `qemu`.

---

## Ejecutar

```bash
./run.sh           # QEMU (busca .qcow2, fallback a .img)
./run.sh vbox      # muestra pasos para VirtualBox
./run.sh usb       # muestra comando dd para grabar en USB
```

**Permiso denegado en la imagen**: el contenedor corre como root, por lo que el `.qcow2` queda con permisos de root. Si `./run.sh` falla:

```bash
sudo chown $USER Image/x64BareBonesImage.qcow2
```

---

## Compilar y correr en un paso

```bash
./compile.sh && ./run.sh              # First-Fit
MM=BUDDY ./compile.sh && ./run.sh     # Buddy System
```

---

## Shell de usuario

Al bootear se inicia la shell interactiva `sh` (`> `). La shell parsea la línea,
crea procesos hijos para los comandos que son procesos y espera (`waitpid`) a
los de foreground antes de mostrar de nuevo el prompt.

### Operadores

| Operador | Significado |
|----------|-------------|
| `cmd &` | Ejecuta `cmd` en **background** (la shell no cede el foreground). |
| `cmd1 \| cmd2` | **Pipe**: conecta el stdout de `cmd1` al stdin de `cmd2`. Ambos lados deben ser comandos de tipo *proceso*. Se soporta un solo `\|` por línea. |

> Los **builtins** (corren sincrónicamente dentro de la shell) **no** admiten `&` ni `|`.

### Atajos de teclado

| Atajo | Acción |
|-------|--------|
| `Ctrl+C` | Mata el proceso en foreground. |
| `Ctrl+D` | Envía EOF al stdin del proceso en foreground. |
| `+` / `-` | Aumenta / disminuye el tamaño de fuente. |
| `Backspace` | Borra el último carácter de la línea. |

### Comandos *builtin* (corren en la shell)

| Comando | Parámetros | Descripción |
|---------|-----------|-------------|
| `help` | — | Lista todos los comandos y operadores. |
| `clear` | — | Limpia la pantalla. |
| `mem` | — | Estado de la memoria: total, usada, libre y bloques asignados. |
| `kill` | `<pid>` | Mata el proceso con ese PID. |
| `nice` | `<pid> <prioridad>` | Cambia la prioridad (1–5) de un proceso. |
| `block` | `<pid>` | Alterna el estado del proceso entre BLOCKED y READY. |

### Comandos *proceso* (admiten `&` y `|`)

| Comando | Parámetros | Descripción |
|---------|-----------|-------------|
| `ps` | — | Lista procesos: PID, prioridad, foreground, estado, stack pointer, base pointer y nombre. |
| `loop` | `[ticks]` | Imprime su PID periódicamente con espera activa (default 18 ticks ≈ 1 s). |
| `cat` | — | Copia stdin a stdout hasta EOF. Funciona standalone (teclado + `Ctrl+D`) y con pipes. |
| `wc` | — | Cuenta las líneas recibidas por stdin. |
| `filter` | — | Reimprime stdin filtrando las vocales. |
| `mvar` | `<escritores> <lectores>` | Demo lectores/escritores con una MVar sincronizada por dos semáforos nombrados. Crea los hijos y termina de inmediato. Antes mata a los hijos de una corrida anterior que sigan vivos. |
| `echo` | `[args...]` | Imprime sus argumentos. |
| `astdin` | — | Emite la letra `a` por stdout (útil con pipes). |
| `red` | — | Imprime stdin en rojo. |
| `test_pipe` | `[bytes]` | Test propio del grupo: productor y consumidor conectados por un pipe. |
| `nread` | `<nombre>` | Abre el pipe nombrado para leer e imprime lo que llega hasta EOF. Espera a que haya un escritor. |
| `nwrite` | `<nombre> [texto]` | Abre el pipe nombrado para escribir y le manda el texto o, si no hay texto, stdin. Espera a que haya un lector. |

### Tests de cátedra

Estos tests también son comandos de tipo *proceso*, por lo que admiten `&` y pueden ejecutarse en foreground o background.

| Test | Parámetros | Descripción |
|------|-----------|-------------|
| `test_mm` | `<max_memoria>` | Test del memory manager (ver abajo). |
| `test_proc` | `<max_procesos>` | Stress de creación/kill/block/unblock de procesos. |
| `test_prio` | `<valor_objetivo>` | Muestra diferencias de ejecución según prioridad. |
| `test_sync` | `<pares> <iteraciones> <use_sem>` | Pares de procesos incrementan/decrementan una variable compartida. |

### Ejemplos

```text
> help                      # lista de comandos
> mem                       # estado de memoria
> loop &                    # lanza loop en background, imprime su PID
> ps                        # ver el PID del loop
> nice 3 5                  # subir la prioridad del PID 3 al máximo
> block 3                   # bloquear/desbloquear el PID 3
> kill 3                    # matar el loop
> cat | wc                  # escribir, terminar con Ctrl+D: imprime # de líneas
> cat | filter              # escribir vocales: se reimprime sin vocales
> mvar 2 2                  # 2 escritores y 2 lectores sobre la MVar
> nread canal &             # espera en background a un escritor de "canal"
> nwrite canal hola         # proceso no relacionado: el lector imprime "hola"
> nread canal | filter      # un pipe nombrado combinado con un pipe de la shell
> test_mm 1000000           # test de memoria en foreground
> test_mm 1000000 &         # ídem en background
> test_sync 2 100 1         # con semáforos: el valor final siempre es 0
> test_sync 2 100 0         # sin semáforos: el valor final varía
> test_prio 1000000         # se ve que las prioridades altas terminan antes
> test_proc 5               # stress de procesos (Ctrl+C para cortar)
```

---

## Test del Memory Manager

Dentro de la shell:

```
test_mm 1000000
```

Es el test de la cátedra: en un bucle infinito pide bloques de tamaño aleatorio
hasta `max_memory`, los inicializa, **verifica su integridad** y los libera. Si
detecta una corrupción imprime `test_mm ERROR` y termina; mientras no imprima
nada, está pasando. Se corta con `Ctrl+C`. Puede correrse en background:
`test_mm 1000000 &`.

Para testear **ambas implementaciones** compilar y ejecutar por separado:

```bash
# First-Fit
./compile.sh && ./run.sh        # → dentro del OS: test_mm 1000000

# Buddy System
MM=BUDDY ./compile.sh && ./run.sh   # → dentro del OS: test_mm 1000000
```

---

## Decisiones de diseño

- **Núcleo sin separación de privilegios ni paginación:** kernel y userland corren en
  ring 0 y comparten espacio de direcciones. Los procesos de usuario solo interactúan
  con el kernel mediante syscalls (`int 0x80`).
- **Memory managers intercambiables:** First-Fit y Buddy System implementan la misma
  interfaz (`memoryManager.h`). La variable `MM` del Makefile elige cuál se compila, de
  modo que una imagen contiene un único administrador.
- **Scheduler:** Round Robin con prioridades 1 a 5. La prioridad determina cuántos
  quanta consecutivos recibe un proceso. Si no hay procesos listos corre `idle`.
- **Procesos:** tabla fija de PCBs y stacks de 8 KB (el kernel corre sobre el mismo stack
  que el proceso durante las syscalls; con Buddy cada stack ocupa 16 KB reales por el
  redondeo a potencia de 2). Un proceso terminado queda ZOMBIE
  solo si su padre todavía puede esperarlo. La shell desliga a los de background con
  `sys_detach`, y el kernel reclama su slot y su `argv` al terminar.
- **Semáforos con nombre:** procesos no relacionados los comparten acordando el nombre.
  La tabla se protege con un lock tomado mediante `xchg`, y los procesos bloqueados
  esperan en una cola por semáforo, sin busy waiting.
- **Pipes:** buffer circular protegido por tres semáforos (`data`, `space`, `mutex`).
  Leer o escribir es transparente porque stdin y stdout de cada proceso se resuelven a
  través de su PCB. La shell los asigna al crear al hijo, por eso no hace falta `dup2`.
- **Pipes nombrados:** procesos no relacionados comparten un pipe acordando el nombre.
  Como un FIFO de POSIX, cada proceso abre un solo extremo y `sys_pipe_open` bloquea
  hasta que el otro extremo esté abierto, sin importar quién llegue primero. El kernel
  registra los extremos abiertos en el PCB y los cierra si el proceso muere sin hacerlo.
- **Builtins y procesos:** los comandos que deben ejecutarse sobre la shell son builtins.
  El resto son procesos, y por eso admiten `&` y `|`.

---

## Limitaciones conocidas

- Se soporta **un solo `|`** por línea (no cadenas de varios pipes).
- Los **builtins** no admiten `&` ni `|`.
- No existe `sys_dup2`: la shell conecta los pipes asignando stdin/stdout del
  hijo al crearlo (`sys_create_process_fd`).
- La tabla de procesos tiene un máximo de `MAX_PROCESSES = 64` procesos simultáneos.
- Un pipe usa tres semáforos de los 32 disponibles, así que entran como mucho 10 pipes
  a la vez aunque la tabla admita `MAX_PIPES = 16`. Un proceso puede tener abiertos
  hasta `MAX_PIPE_FDS = 8` extremos, y el nombre de un pipe admite hasta 15 caracteres.
- Los datos de un pipe nombrado se pierden al cerrarse su último extremo.
- Solo el **foreground** lee del teclado: un proceso de background que lee stdin
  recibe EOF. Por eso `cat &` termina de inmediato.
- `idle` y `sh` no se pueden matar ni bloquear.
- `kill` y `Ctrl+C` matan también a los descendientes del proceso. Un proceso que
  termina normalmente deja a sus hijos corriendo (`mvar` termina apenas los crea).
- Un proceso matado dentro de su sección crítica no devuelve su token: el kernel no
  sabe qué semáforos tiene tomados, y quienes esperen ese semáforo quedan bloqueados.
- La prioridad de un proceso solo cambia su parte de CPU. Con los valores por defecto,
  subir un escritor de `mvar` a 5 le da 5 ticks contra 3 del otro.
- Sin paginación: todos los procesos comparten el mismo espacio de direcciones.
- `block` no despierta a un proceso que está esperando en `waitpid`: solo lo despierta
  la terminación de su hijo.
- `test_mm <max_memoria>` debería usar un valor menor al heap libre (8 MB). Con valores
  mayores puede quedar reintentando reservas sin imprimir nada.
- El árbol conserva **comandos heredados de x64BareBones** (arquitectura, benchmarks
  `bm*`, manejo de excepciones) que **no forman parte de la entrega de TP2**: no están
  registrados en la tabla `commands[]` de la shell y se mantienen solo como referencia.

---

## Problemas encontrados y soluciones

- **Triple fault al arrancar un proceso:** en long mode `iretq` siempre extrae RIP, CS,
  RFLAGS, RSP y SS. El stack inicial se arma con el frame completo.
- **Procesos cuyo entry retorna sin `sys_exit`:** el `ret` saltaba a basura porque el
  stack inicial no tiene dirección de retorno. Todo proceso arranca en un trampolín que
  llama al entry y luego termina el proceso.
- **Bytes fantasma al reabrir un pipe nombrado:** al cerrarse el último extremo se
  posteaba el semáforo `MAX_PROCESSES` veces para despertar a los bloqueados. Esos tokens
  quedaban para siempre, y al reabrir el pipe un lector recibía bytes que nadie escribió.
  Ahora `sem_wake_all` despierta sin entregar tokens, y cada semáforo cuenta exactamente
  los bytes o los lugares libres.
- **Rendezvous de pipes nombrados:** un escritor podía abrir, escribir y cerrar antes de
  que volviera a correr el lector que lo esperaba, que veía cero escritores y se volvía
  a dormir. La espera también mira cuántas aperturas hubo, igual que Linux.
- **Fuga de slots de procesos:** los procesos terminados que nadie esperaba ocupaban la
  tabla hasta llenarla. Se agregaron la liberación del slot cuando el padre ya no puede
  esperar, y `sys_detach` para los procesos de background.
- **Ctrl+C mataba a la shell:** solo se registran como foreground los procesos creados
  por otro proceso, no los creados durante la inicialización del kernel.
- **Argumentos inválidos en los hijos:** el `argv` apuntaba a memoria del padre que
  podía dejar de ser válida. La shell lo copia al heap y `mvar` usa almacenamiento
  estático.
- **Valores viejos en semáforos con nombre:** `sem_open` sobre un nombre existente
  conserva su valor, lo que rompía `mvar` y `test_sync` entre corridas. Estos comandos
  purgan sus semáforos antes de abrirlos.
- **`block` sobre un padre en `waitpid`:** `process_unblock` pasaba a READY a cualquier
  proceso bloqueado, y `waitpid` retornaba con basura mientras el hijo seguía vivo. Ahora
  se ignora a los procesos con `waiting_for != 0`.
- **`mvar` corrido dos veces seguidas:** los escritores y lectores viejos se enganchaban a
  los semáforos de la corrida nueva (mismos nombres). Ahora `mvar` mata a los anteriores
  antes de purgar los semáforos.
- **`test_proc` dejaba procesos huérfanos al fallar:** `process_exit` no mata a los hijos,
  así que los `endless_loop` ya creados seguían corriendo. Ahora el test los mata antes de
  salir, y los wrappers `my_kill`, `my_block` y `my_unblock` devuelven el resultado real.
- **Stacks de 4 KB al límite:** `test_mm` usa unos 2 KB de stack y el kernel corre sobre el
  mismo, sin guard page. Se subió a 8 KB.
- **`help` perdía líneas con la fuente agrandada:** la consola no tiene scrollback. Se
  agregó un paginador que cuenta filas físicas.

---

## Análisis estático (PVS-Studio)

Se analizó el proyecto dentro de la imagen de la cátedra:

```bash
make clean
pvs-studio-analyzer trace -- make all
pvs-studio-analyzer analyze -o PVS-Studio.log
plog-converter -a GA:1,2 -t tasklist -o report.tasks PVS-Studio.log
```

**Resultado:** COMPLETAR después de correrlo (sin advertencias, o la lista de abajo).

Falsos positivos justificados:

- COMPLETAR: `archivo.c:línea` (código V###): por qué no es un problema real.

---

## Citas de fragmentos de código

- **x64BareBones:** base del proyecto, proveniente del TP de Arquitectura de
  Computadoras (se conservan comandos heredados, ver *Limitaciones conocidas*).
- **Pure64 y BMFS:** bootloader y sistema de archivos incluidos en `Bootloader/`.
- **Tests de la cátedra:** `test_mm`, `test_proc`, `test_prio` y `test_sync` están
  adaptados de los tests provistos.

---

## Limpiar artefactos

```bash
make clean
```

---

## Uso de IA

Durante el desarrollo se utilizó **Claude Code** (Anthropic) como asistente para
revisión de código, detección de bugs y redacción de documentación/comentarios.
Las decisiones de diseño, la integración y la verificación finales fueron
realizadas y validadas por los integrantes del grupo.
