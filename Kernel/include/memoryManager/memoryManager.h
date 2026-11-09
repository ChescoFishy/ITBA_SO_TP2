#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stdint.h>

#define KERNEL 1
#define USER 0

/* Estado de memoria compartido por ambos managers. */
typedef struct {
    uint64_t total;         /* Bytes totales administrados. */ 
    uint64_t used;          /* Bytes actualmente asignados. */
    uint64_t free;          /* Bytes libres. */
    uint64_t alloc_count;   /* Cantidad de bloques asignados actualmente. */
} MemStatus;

/* Inicializa el administrador de memoria. */
void mm_init(void *start, uint64_t size);

/* Reserva memoria para un proceso. Retorna NULL si falla. */
void *mm_malloc(uint64_t size, uint64_t owner);

/* Reserva memoria del kernel, sin contarla en alloc_count. */
void *mm_malloc_kernel(uint64_t size);

/* Libera un bloque de memoria. Ignora NULL. */
void mm_free(void *ptr);

/* Libera toda la memoria asignada a un proceso. */
void mm_free_owned(uint64_t owner);

/* Obtiene el estado actual de la memoria. */
void mm_status(MemStatus *status);

#endif