#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stdint.h>

#define KERNEL 1
#define USER 0

/* Estructura para reportar el estado de la memoria. Ambos managers deben usar la misma estructura. */
typedef struct {
    uint64_t total;         /* Bytes totales administrados. */ 
    uint64_t used;          /* Bytes actualmente asignados. */
    uint64_t free;          /* Bytes libres. */
    uint64_t alloc_count;   /* Cantidad de bloques asignados actualmente. */
} MemStatus;

/* Inicializa el memory manager sobre el bloque [start, start+size) */
void mm_init(void *start, uint64_t size);

/* Reserva 'size' bytes a nombre del proceso 'owner' (ver mm_free_owned).
   Retorna NULL si no hay espacio. */
void *mm_malloc(uint64_t size, uint64_t owner);

/* Igual que mm_malloc pero marca el bloque como interno del kernel: no se
   contabiliza en alloc_count, de modo que mm_status refleje solo las
   allocaciones hechas por el usuario (via sys_malloc). */
void *mm_malloc_kernel(uint64_t size);

/* Libera el bloque apuntado por 'ptr'. Si ptr es NULL, no hace nada. */
void mm_free(void *ptr);

/* Libera todos los bloques reservados a nombre de 'owner'. Se usa al terminar un
   proceso: uno matado no llega a liberar su memoria por su cuenta. */
void mm_free_owned(uint64_t owner);

/* Llena 'status' con el estado actual de la memoria. */ 
void mm_status(MemStatus *status);

#endif