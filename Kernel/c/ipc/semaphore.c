#include "ipc/semaphore.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "interrupts/interrupts.h"
#include <stddef.h>

/* Primitivas atomicas (xchg) definidas en libasm.asm. Protegen el acceso al
** valor del semaforo contra race conditions, segun pide el enunciado. */
extern void acquire(uint64_t *lock);
extern void release(uint64_t *lock);

/* Un unico lock global protege toda la tabla de semaforos. Las syscalls corren
** con IF=0 (int 0x80 es un interrupt gate) y lo sueltan antes de ceder la CPU,
** asi que nunca esta tomado con interrupciones habilitadas. Por eso el Ctrl+C
** del teclado, que puede llegar aca via process_kill, no puede encontrarlo
** tomado: no hay riesgo de deadlock por reentrancia. */
static uint64_t sem_lock = 0;

#define MAX_SEMS     32
#define SEM_NAME_LEN 32

typedef struct {
    char     name[SEM_NAME_LEN];
    int64_t  value;
    uint64_t wait_queue[MAX_PROCESSES]; /* PIDs bloqueados esperando este sem */
    int      wait_head;
    int      wait_tail;
    int      wait_count;
    int      open_count; /* 0 = entrada libre en la tabla */
} Semaphore;

static Semaphore sem_table[MAX_SEMS];

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int sem_str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a == (unsigned char)*b;
}

static void sem_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static Semaphore *find_sem(const char *name) {
    int i;
    for (i = 0; i < MAX_SEMS; i++)
        if (sem_table[i].open_count > 0 && sem_str_eq(sem_table[i].name, name))
            return &sem_table[i];
    return NULL;
}

static void queue_push(Semaphore *s, uint64_t pid) {
    if (s->wait_count >= MAX_PROCESSES) return;
    s->wait_queue[s->wait_tail] = pid;
    s->wait_tail = (s->wait_tail + 1) % MAX_PROCESSES;
    s->wait_count++;
}

static uint64_t queue_pop(Semaphore *s) {
    uint64_t pid = s->wait_queue[s->wait_head];
    s->wait_head = (s->wait_head + 1) % MAX_PROCESSES;
    s->wait_count--;
    return pid;
}

/* Saca un PID de la cola conservando el orden FIFO de los demas. */
static void queue_remove(Semaphore *s, uint64_t pid) {
    int kept = 0;
    int i;
    for (i = 0; i < s->wait_count; i++) {
        uint64_t queued = s->wait_queue[(s->wait_head + i) % MAX_PROCESSES];
        if (queued != pid) {
            s->wait_queue[(s->wait_head + kept) % MAX_PROCESSES] = queued;
            kept++;
        }
    }
    s->wait_count = kept;
    s->wait_tail  = (s->wait_head + kept) % MAX_PROCESSES;
}

/* Despierta al primero de la cola informandole como termino su espera. */
static void wake_first(Semaphore *s, SemWaitStatus status) {
    PCB *waiter = process_get(queue_pop(s));
    if (waiter != NULL) {
        waiter->sem_wait = status;
        process_unblock(waiter->pid);
    }
}

/* Entrega el token: si hay alguien esperando se lo pasa directamente, sin que
** value suba. Asi ningun proceso que llegue despues puede ganarle el token a
** los que ya estan en la cola (FIFO), lo que evita la inanicion. */
static void release_token(Semaphore *s) {
    if (s->wait_count > 0)
        wake_first(s, SEM_WAIT_GRANTED);
    else
        s->value++;
}

/* ── API pública ─────────────────────────────────────────────────────────── */

void sem_init(void) {
    int i, j;
    for (i = 0; i < MAX_SEMS; i++) {
        sem_table[i].open_count = 0;
        sem_table[i].value      = 0;
        sem_table[i].wait_head  = 0;
        sem_table[i].wait_tail  = 0;
        sem_table[i].wait_count = 0;
        sem_table[i].name[0]    = '\0';
        for (j = 0; j < MAX_PROCESSES; j++)
            sem_table[i].wait_queue[j] = 0;
    }
}

/* Abre (o crea) un semáforo por nombre. Retorna 1 en éxito, 0 si tabla llena. */
int64_t sem_open(const char *name, uint64_t initial_value) {
    if (!name) return 0;

    acquire(&sem_lock);

    /* ¿Ya existe? */
    Semaphore *s = find_sem(name);
    if (s) {
        s->open_count++;
        release(&sem_lock);
        return 1;
    }

    /* Buscar slot libre */
    int i;
    for (i = 0; i < MAX_SEMS; i++) {
        if (sem_table[i].open_count == 0) {
            s = &sem_table[i];
            break;
        }
    }
    if (!s) {
        release(&sem_lock);
        return 0;
    }

    sem_str_copy(s->name, name, SEM_NAME_LEN);
    s->value      = (int64_t)initial_value;
    s->open_count = 1;
    s->wait_head  = 0;
    s->wait_tail  = 0;
    s->wait_count = 0;
    release(&sem_lock);
    return 1;
}

/* Decrementa el semáforo; bloquea el proceso actual si no hay tokens.
** Bloqueo REAL: cede la CPU via kernel_yield (int 0x81) y recién retorna con
** el token en su poder, que sem_post le entrega directamente. Esto permite
** llamarlo desde adentro del kernel (pipe_read/pipe_write) con código después
** del wait. Retorna 0 con el token, o -1 si el semáforo no existe o lo despertaron
** sin token (se destruyó mientras esperaba, o sem_wake_all). */
int64_t sem_wait(const char *name) {
    if (!name) return -1;

    acquire(&sem_lock);

    Semaphore *s = find_sem(name);
    if (!s) {
        release(&sem_lock);
        return -1;
    }

    if (s->value > 0) {
        s->value--;
        release(&sem_lock);
        return 0;
    }

    PCB *cur = process_current();
    if (cur == NULL) {
        release(&sem_lock);
        return -1;
    }

    queue_push(s, cur->pid);
    cur->sem_slot = (int)(s - sem_table);
    cur->sem_wait = SEM_WAIT_PENDING;
    process_block(cur->pid);

    /* Soltar el lock ANTES de ceder la CPU (nadie puede correr en el medio: las
    ** syscalls corren con IF=0). Duerme acá hasta que un sem_post le entregue
    ** el token. Si lo despertaron sin token (sys_unblock o el comando block),
    ** sigue en la cola y vuelve a dormirse: no puede entrar sin el token. */
    release(&sem_lock);
    kernel_yield();
    while (cur->sem_wait == SEM_WAIT_PENDING) {
        process_block(cur->pid);
        kernel_yield();
    }

    int64_t result = (cur->sem_wait == SEM_WAIT_GRANTED) ? 0 : -1;
    cur->sem_wait = SEM_WAIT_NONE;
    return result;
}

/* Incrementa el semáforo o, si hay alguien esperando, le entrega el token. */
int64_t sem_post(const char *name) {
    if (!name) return -1;

    acquire(&sem_lock);

    Semaphore *s = find_sem(name);
    if (!s) {
        release(&sem_lock);
        return -1;
    }

    release_token(s);

    release(&sem_lock);
    return 0;
}

/* Decrementa el contador de usuarios; libera la entrada si llega a 0. Quien
** siguiera esperando se despierta con error en lugar de quedar bloqueado para
** siempre en un semáforo que ya no existe. */
int64_t sem_close(const char *name) {
    if (!name) return -1;

    acquire(&sem_lock);

    Semaphore *s = find_sem(name);
    if (!s) {
        release(&sem_lock);
        return -1;
    }

    s->open_count--;
    if (s->open_count <= 0) {
        while (s->wait_count > 0)
            wake_first(s, SEM_WAIT_NO_TOKEN);
        s->open_count = 0;
        s->name[0]    = '\0';
        s->wait_head  = 0;
        s->wait_tail  = 0;
    }

    release(&sem_lock);
    return 0;
}

/* Despierta a toda la cola sin entregar tokens: el valor sigue contando lo mismo y
** cada sem_wait devuelve -1. Sirve para avisar un cambio ajeno al contador, como que
** se cerro el otro extremo de un pipe; quien despierta debe revisar su condicion. */
int64_t sem_wake_all(const char *name) {
    if (!name) return -1;

    acquire(&sem_lock);

    Semaphore *s = find_sem(name);
    if (!s) {
        release(&sem_lock);
        return -1;
    }

    while (s->wait_count > 0)
        wake_first(s, SEM_WAIT_NO_TOKEN);

    release(&sem_lock);
    return 0;
}

/* Un proceso que muere no puede seguir ocupando la cola: sem_post gastaria en
** el su unico despertar y el siguiente quedaria bloqueado con un token libre.
** Si ya le habian entregado el token pero no llego a usarlo, se devuelve para
** que no se pierda. El token de un proceso muerto dentro de su seccion critica
** (ya retorno de sem_wait) no se puede recuperar: el kernel no sabe que lo tiene. */
void sem_cancel_wait(PCB *p) {
    if (p->sem_wait == SEM_WAIT_NONE) return;

    acquire(&sem_lock);

    Semaphore *s = &sem_table[p->sem_slot];
    if (p->sem_wait == SEM_WAIT_PENDING)
        queue_remove(s, p->pid);
    else if (p->sem_wait == SEM_WAIT_GRANTED && s->open_count > 0)
        release_token(s);
    p->sem_wait = SEM_WAIT_NONE;

    release(&sem_lock);
}
