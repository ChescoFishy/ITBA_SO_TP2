#include "process/scheduler.h"
#include "process/process.h"
#include "time/time.h"

/* Indica si se debe forzar un cambio de contexto. */
volatile uint64_t force_switch = 0;

/* Cola de procesos ejecutables. */
static PCB* run_queue[MAX_PROCESSES];

static int queue_size = 0;
static int queue_idx = 0;

/* Se ejecuta cuando no hay otros procesos READY. */
static PCB* idle_proc = NULL;

/* Inicializa la cola de ejecución. */
void scheduler_init(void){
    queue_size = 0;         /* Tamaño de la cola. */
    queue_idx = 0;          /* Índice del proceso actual en la cola (round-robin). */
    idle_proc = NULL;
}

/* Registra el proceso idle para que el scheduler lo trate como fallback. */
void scheduler_set_idle(PCB* p){
    idle_proc = p;
}

/* Inicia la ejecucion del primer proceso READY. */
void scheduler_start(void){
    PCB* first = scheduler_next_ready();

    if(first == NULL){
        return;
    }

    first->state = PROCESS_RUNNING;
    process_set_current(first);

    /* Definida en interrupts.asm; no retorna. */
    scheduler_start_asm(first->rsp);
}

/* Agrega un proceso a la cola de ejecución. */
void scheduler_add(PCB* p){

    if(queue_size < MAX_PROCESSES){
        run_queue[queue_size++] = p;
    }
}

/* Elimina un proceso de la cola de ejecución. */
void scheduler_remove(PCB* p){

    for(int i = 0; i < queue_size; i++){

        if(p == run_queue[i]){

            /* Reemplazar con el último proceso. */
            run_queue[i] = run_queue[--queue_size]; 

            if((queue_idx >= queue_size) && (queue_size > 0)){
                queue_idx = queue_size - 1;
            }

            return;
        }
    }
}

/* Busca el siguiente proceso READY mediante Round-Robin.
   Prioriza procesos normales y utiliza idle como último recurso. */
PCB* scheduler_next_ready(void){
    if(queue_size == 0){
        return NULL;
    }

    PCB* idle_ready = NULL;   /* idle se reserva como ultima instancia */

    for(int i = 0; i < queue_size; i++){
        queue_idx = (queue_idx + 1) % queue_size;

        PCB* c = run_queue[queue_idx];

        if(c->state == PROCESS_READY){
            if(c == idle_proc){
                /* No elegir idle si hay otro READY: lo dejo como fallback. */
                idle_ready = c;
                continue;
            }
            return c;
        }
    }

    /* Ningun proceso no-idle listo: correr idle si estaba READY, sino NULL. */
    return idle_ready;
}

/* Ejecutado en cada tick del timer.
   Aplica Round-Robin con prioridades: cada proceso ejecuta
   tantos quanta consecutivos como indique su prioridad. */
uint64_t* scheduler_tick(uint64_t* current_rsp){

    timer_handler();

    PCB* cur = process_current();

    if(cur != NULL){
        cur->rsp = current_rsp;

        if(cur->state == PROCESS_RUNNING){
            if(cur->remaining_quanta > 0){
                cur->remaining_quanta--;
            }

            if(cur->remaining_quanta > 0){
                return current_rsp;
            }

            /* Quantum agotado -> pasa a READY y reinicia el contador. */
            cur->state = PROCESS_READY;
            cur->remaining_quanta = cur->priority;
        }
    }

    /* Elegir el siguiente proceso READY. */
    PCB* next = scheduler_next_ready();

    if(next == NULL){
        /* Si no hay otro proceso listo, continúa con el actual si puede.
        De lo contrario, conserva el contexto actual. */
        if((cur != NULL) && (cur->state == PROCESS_READY || cur->state == PROCESS_RUNNING)){
            cur->state = PROCESS_RUNNING;
            return current_rsp;
        }

        return current_rsp;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    return next->rsp;
}

/* Realiza un cambio de contexto voluntario.
   Se utiliza en yield, exit, block y otras operaciones bloqueantes. */
uint64_t *scheduler_yield_impl(uint64_t *current_rsp){
    PCB* cur = process_current();

    if(cur != NULL){
        cur->rsp = current_rsp;

        if(cur->state == PROCESS_RUNNING){
            /* Lo pauso (si no fue ya BLOCKED/ZOMBIE por la syscall). */
            cur->state = PROCESS_READY;
        }

        /* Reiniciar quanta para la proxima vez que sea elegido. */
        cur->remaining_quanta = cur->priority;
    }

    PCB* next = scheduler_next_ready();
    if(next == NULL){
        /* Retoma el proceso actual si sigue READY. */
        if(cur != NULL && cur->state == PROCESS_READY){
            cur->state = PROCESS_RUNNING;
            return current_rsp;
        }
        /* Sin procesos listos, conserva el contexto actual.
        Idle debería evitar esta situación. */
        return current_rsp;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    return next->rsp;
}


