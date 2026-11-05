#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <stdint.h>
#include "process/process.h"

void sem_init(void);
int64_t sem_open(const char *name, uint64_t initial_value);
int64_t sem_wait(const char *name);
int64_t sem_post(const char *name);
int64_t sem_close(const char *name);

/* Retira de su semaforo a un proceso que muere mientras espera. */
void sem_cancel_wait(PCB *p);

#endif
