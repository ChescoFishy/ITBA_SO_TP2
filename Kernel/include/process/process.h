#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 64
#define STACK_SIZE (4 * 1024)    // 4 KB por proceso
#define MAX_NAME_LEN 32
#define MIN_PRIORITY 1
#define MAX_PRIORITY 5
#define DEFAULT_PRIORITY 3

typedef enum {
    PROCESS_FREE = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_BLOCKED = 3,
    PROCESS_ZOMBIE  = 4
} ProcessState;

/* Situacion de un proceso respecto de un sem_wait bloqueante. */
typedef enum {
    SEM_WAIT_NONE = 0,  /* No esta esperando ningun semaforo */
    SEM_WAIT_PENDING,   /* En la cola, todavia sin token */
    SEM_WAIT_GRANTED,   /* sem_post ya le entrego el token, falta que corra */
    SEM_WAIT_CLOSED     /* El semaforo se destruyo mientras esperaba */
} SemWaitStatus;

typedef void (*ProcessEntry)(int argc, char **argv);

/* Tabla de procesos para Kernel. */
typedef struct PCB{
    uint64_t pid;               /* PID del proceso */
    char name[MAX_NAME_LEN];    /* Nombre del proceso. */
    uint8_t priority;           /* Prioridad del proceso (1-5). */
    uint8_t remaining_quanta;   /* Cuanta restante en el quantum actual. */
    ProcessState state;         /* Estado del proceso. */
    uint64_t *rsp;              /* RSP guardado (valido cuando no esta RUNNING) */
    uint64_t *stack_base;       /* base del stack mm_malloc (para liberar) */
    uint8_t foreground;         /* Indica si el proceso es foreground (1) o background (0). */
    int fd[2];                  /* fd[0]=stdin, fd[1]=stdout */
    uint64_t parent_pid;        /* PID del proceso padre (0 si no tiene padre) */
    uint64_t waiting_for;       /* PID esperado en waitpid (0 = no espera) */   
    int argc;                   /* Cantidad de argumentos (para pasar a la función entry) */
    char **argv;                /* Argumentos (para pasar a la función entry) */
    int retval;                 /* Valor de retorno del proceso (para que el padre lo lea en waitpid) */
    uint8_t detached;           /* 1 si nadie hara waitpid: el kernel reclama slot y argv al terminar */
    SemWaitStatus sem_wait;     /* Estado de su sem_wait bloqueante */
    int sem_slot;               /* Semaforo esperado (valido si sem_wait != SEM_WAIT_NONE) */
} PCB;

/* Vista userland de un proceso diferenciado de Kernel. */
typedef struct{
    uint64_t pid;
    char name[MAX_NAME_LEN];
    uint8_t priority;
    uint8_t state;
    uint8_t foreground;
    uint64_t rsp;
    uint64_t rbp;
} ProcessInfo;

void process_init(void);
int process_create(const char *name, ProcessEntry entry, int argc, char **argv, uint8_t fg);
int process_create_fd(const char *name, ProcessEntry entry, int argc, char **argv,
                      uint8_t fg, int fd_in, int fd_out);
void process_exit(int retval);
PCB* process_get(uint64_t pid);
PCB* process_current(void);
void process_set_current(PCB *p);
void process_kill(uint64_t pid);
void process_block(uint64_t pid);
void process_unblock(uint64_t pid);
void process_nice(uint64_t pid, uint8_t new_priority);
int process_waitpid(uint64_t pid);

/* Desliga un hijo del llamante: nadie hara waitpid sobre el, asi que al terminar
** el kernel libera su slot y su argv (debe ser un bloque de sys_malloc o NULL).
** Devuelve 0 en exito; -1 si pid no es un hijo vivo o zombie del llamante, en cuyo
** caso el argv sigue siendo responsabilidad del llamante. */
int process_detach(uint64_t pid);
uint64_t process_ps(ProcessInfo *buf, uint64_t max);

/* Devuelve el PID del ultimo proceso foreground creado por la shell.
** 0 si no hay ninguno vivo. Lo usa el driver de teclado para Ctrl+C. */
uint64_t process_get_foreground(void);

#endif
