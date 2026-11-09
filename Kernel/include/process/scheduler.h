#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "process/process.h"

/* Solicita un cambio de contexto voluntario desde interrupts.asm. */
extern volatile uint64_t force_switch;

/* Inicializa la cola de ejecución. */
void scheduler_init(void);

/* Inicia la ejecución del primer proceso sin retornar. */
void scheduler_start(void);

/* Restaura el contexto inicial mediante popState + iretq. */
void scheduler_start_asm(uint64_t* rsp);

/* Agrega un proceso a la cola. */
void scheduler_add(PCB* p);

/* Elimina un proceso de la cola. */
void scheduler_remove(PCB* p);

/* Registra idle como alternativa cuando no hay otros procesos READY. */
void scheduler_set_idle(PCB* p);

/* Selecciona el siguiente proceso READY mediante Round-Robin.
   Devuelve NULL si no hay procesos disponibles. */
PCB* scheduler_next_ready(void);

/* Ejecutado en cada tick del timer.
   Aplica Round-Robin con prioridades, asignando tantos quanta
   consecutivos como indique la prioridad del proceso. */
uint64_t* scheduler_tick(uint64_t* current_rsp);

/* Realiza un cambio de contexto voluntario solicitado por force_switch. */
uint64_t* scheduler_yield_impl(uint64_t* current_rsp);

#endif