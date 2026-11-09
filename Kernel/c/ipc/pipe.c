#include "ipc/pipe.h"
#include "ipc/semaphore.h"
#include "process/process.h"
#include "console/naiveConsole.h"
#include <stddef.h>

/* Los que esperan en pipe_wait_peer duermen en un unico semaforo, compartido por
** todos los pipes: cada apertura los despierta y cada uno revisa su condicion. */
#define PIPE_OPEN_SEM "__pipe_open"

typedef struct {
    int      in_use;
    char     buffer[PIPE_BUF_SIZE];
    int      head;       /* posicion de lectura */
    int      tail;       /* posicion de escritura */
    int      count;      /* bytes actualmente almacenados */
    int      readers;    /* cantidad de extremos de lectura abiertos */
    int      writers;    /* cantidad de extremos de escritura abiertos */
    uint64_t read_opens;  /* veces que se abrio el extremo de lectura */
    uint64_t write_opens; /* veces que se abrio el extremo de escritura */
    char     data_sem[16];   /* un token por cada byte almacenado */
    char     space_sem[16];  /* un token por cada lugar libre */
    char     mutex_sem[16];  /* exclusion mutua sobre buffer */
    char     name[PIPE_NAME_LEN]; /* identificador a priori; vacio si es anonimo */
} Pipe;

static Pipe pipes[MAX_PIPES];

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void build_name(char *out, const char *prefix, int idx) {
    int i = 0;
    while (prefix[i]) { out[i] = prefix[i]; i++; }
    char num[4];
    uintToBase((uint64_t)idx, num, 10);
    int j = 0;
    while (num[j]) { out[i++] = num[j++]; }
    out[i] = '\0';
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a == (unsigned char)*b;
}

static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Evita nombres vacios o truncados que puedan generar conflictos. */
static int name_is_valid(const char *name) {
    if (!name || !name[0]) return 0;
    int len = 0;
    while (name[len] && len < PIPE_NAME_LEN) len++;
    return len < PIPE_NAME_LEN;
}

static int fd_to_index(int fd) {
    if (fd >= PIPE_FD_WRITE_BASE && fd < PIPE_FD_WRITE_BASE + MAX_PIPES)
        return fd - PIPE_FD_WRITE_BASE;
    if (fd >= PIPE_FD_READ_BASE && fd < PIPE_FD_READ_BASE + MAX_PIPES)
        return fd - PIPE_FD_READ_BASE;
    return -1;
}

static int fd_is_write(int fd) {
    return fd >= PIPE_FD_WRITE_BASE && fd < PIPE_FD_WRITE_BASE + MAX_PIPES;
}

/* Reserva un pipe y sus semáforos, sin extremos abiertos. */
static int pipe_alloc(void) {
    int i;
    for (i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].in_use) break;
    }
    if (i == MAX_PIPES) return -1;

    Pipe *p = &pipes[i];
    p->head    = 0;
    p->tail    = 0;
    p->count   = 0;
    p->readers = 0;
    p->writers = 0;
    p->read_opens  = 0;
    p->write_opens = 0;
    p->name[0] = '\0';

    build_name(p->data_sem,  "__pipe_d_", i);
    build_name(p->space_sem, "__pipe_s_", i);
    build_name(p->mutex_sem, "__pipe_m_", i);

    if (!sem_open(p->data_sem,  0) ||
        !sem_open(p->space_sem, PIPE_BUF_SIZE) ||
        !sem_open(p->mutex_sem, 1)) {
        /* limpieza parcial: cerrar los que se hayan abierto */
        sem_close(p->data_sem);
        sem_close(p->space_sem);
        sem_close(p->mutex_sem);
        return -1;
    }

    p->in_use = 1;
    return i;
}

/* Espera un token de datos o devuelve EOF si no quedan escritores. */
static int wait_for_data(Pipe *p) {
    while (p->count > 0 || p->writers > 0) {
        if (sem_wait(p->data_sem) == 0) return 1;
    }
    return 0;
}

/* Espera un token de espacio o devuelve 0 si no quedan lectores. */
static int wait_for_space(Pipe *p) {
    while (p->readers > 0) {
        if (sem_wait(p->space_sem) != 0) continue;
        if (p->readers > 0) return 1;
        sem_post(p->space_sem); /* se fueron los lectores antes de que corriera */
    }
    return 0;
}

/* ── API ─────────────────────────────────────────────────────────────────── */

void pipe_init(void) {
    int i;
    for (i = 0; i < MAX_PIPES; i++) {
        pipes[i].in_use  = 0;
        pipes[i].head    = 0;
        pipes[i].tail    = 0;
        pipes[i].count   = 0;
        pipes[i].readers = 0;
        pipes[i].writers = 0;
        pipes[i].name[0] = '\0';
    }
    sem_open(PIPE_OPEN_SEM, 0);
}

int pipe_is_fd(int fd) {
    return fd_to_index(fd) >= 0;
}

/* Incrementa los extremos heredados por un proceso hijo. */
int pipe_inherit_fd(int fd) {
    int idx = fd_to_index(fd);
    if (idx < 0) return -1;
    Pipe *p = &pipes[idx];
    if (!p->in_use) return -1;
    if (fd_is_write(fd)) p->writers++;
    else                 p->readers++;
    return 0;
}

int pipe_create(int fds[2]) {
    if (!fds) return -1;

    int i = pipe_alloc();
    if (i < 0) return -1;

    pipes[i].readers = 1;
    pipes[i].writers = 1;

    fds[0] = PIPE_FD_READ_BASE  + i;
    fds[1] = PIPE_FD_WRITE_BASE + i;
    return 0;
}

/* Abre o crea un pipe nombrado y devuelve el fd correspondiente. */
int pipe_open(const char *name, int mode) {
    if (!name_is_valid(name)) return -1;
    if (mode != PIPE_OPEN_READ && mode != PIPE_OPEN_WRITE) return -1;

    int i;
    for (i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].in_use && str_eq(pipes[i].name, name)) break;
    }
    if (i == MAX_PIPES) {
        i = pipe_alloc();
        if (i < 0) return -1;
        str_copy_n(pipes[i].name, name, PIPE_NAME_LEN);
    }

    if (mode == PIPE_OPEN_WRITE) {
        pipes[i].writers++;
        pipes[i].write_opens++;
    } else {
        pipes[i].readers++;
        pipes[i].read_opens++;
    }
    sem_wake_all(PIPE_OPEN_SEM);

    return (mode == PIPE_OPEN_WRITE) ? PIPE_FD_WRITE_BASE + i : PIPE_FD_READ_BASE + i;
}

/* Espera la apertura del extremo opuesto. Considera aperturas previas para evitar esperas innecesarias. */
void pipe_wait_peer(int fd) {
    int idx = fd_to_index(fd);
    if (idx < 0) return;
    Pipe *p = &pipes[idx];
    int is_write = fd_is_write(fd);
    uint64_t seen = is_write ? p->read_opens : p->write_opens;

    while (p->in_use &&
           (is_write ? p->readers : p->writers) == 0 &&
           (is_write ? p->read_opens : p->write_opens) == seen) {
        sem_wait(PIPE_OPEN_SEM);
    }
}

int pipe_close(int fd) {
    int idx = fd_to_index(fd);
    if (idx < 0) return -1;
    Pipe *p = &pipes[idx];
    if (!p->in_use) return -1;

    /* Despierta a los bloqueados cuando se cierra el ultimo extremo,
   sin alterar los tokens de datos o espacio. */
    if (fd_is_write(fd)) {
        if (p->writers > 0) p->writers--;
        if (p->writers == 0) sem_wake_all(p->data_sem);
    } else {
        if (p->readers > 0) p->readers--;
        if (p->readers == 0) sem_wake_all(p->space_sem);
    }

    if (p->readers == 0 && p->writers == 0) {
        sem_close(p->data_sem);
        sem_close(p->space_sem);
        sem_close(p->mutex_sem);
        p->in_use = 0;
        p->name[0] = '\0';
    }
    return 0;
}

int64_t pipe_read(int fd, char *buf, uint64_t n) {
    int idx = fd_to_index(fd);
    if (idx < 0 || fd_is_write(fd)) return -1;
    Pipe *p = &pipes[idx];
    if (!p->in_use || !buf) return -1;
    if (n == 0) return 0;

    uint64_t read = 0;
    while (read < n) {
        if (!wait_for_data(p)) return (int64_t)read;

        sem_wait(p->mutex_sem);
        buf[read++] = p->buffer[p->head];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
        p->count--;
        sem_post(p->mutex_sem);

        sem_post(p->space_sem);

        /* Devuelve los bytes leidos sin esperar nuevos datos. */
        if (p->count == 0) {
            /* Despierta lectores bloqueados si no quedan datos ni escritores. */
            if (p->writers == 0) sem_wake_all(p->data_sem);
            return (int64_t)read;
        }
    }
    return (int64_t)read;
}

int64_t pipe_write(int fd, const char *buf, uint64_t n) {
    int idx = fd_to_index(fd);
    if (idx < 0 || !fd_is_write(fd)) return -1;
    Pipe *p = &pipes[idx];
    if (!p->in_use || !buf) return -1;
    if (n == 0) return 0;

    uint64_t written = 0;
    while (written < n) {
        if (!wait_for_space(p)) return -1;  /* broken pipe */

        sem_wait(p->mutex_sem);
        p->buffer[p->tail] = buf[written++];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
        p->count++;
        sem_post(p->mutex_sem);

        sem_post(p->data_sem);
    }
    return (int64_t)written;
}
