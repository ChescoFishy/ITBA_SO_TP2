#include <stdint.h>
#include "lib/userlib.h"
#include "lib/io.h"
#include "lib/strings.h"
#include "tests/test_util.h"
#include "commands/commands.h"

/* mvar: N escritores y M lectores sobre una celda compartida, sincronizados
** con dos semaforos (empty/full). Los hijos quedan en background. */

/* Espera activa en ticks. Los escritores esperan mas que los lectores para
** que el ritmo lo marquen ellos y no la cola del semaforo. */
#define WRITER_WAIT_MIN    8
#define WRITER_WAIT_SPREAD 2
#define READER_WAIT_MIN    1
#define READER_WAIT_SPREAD 2

#define CALIBRATION_SPINS 1000
#define MAX_WRITERS  26
#define MAX_READERS  7

#define SEM_EMPTY "mvar_empty"
#define SEM_FULL  "mvar_full"

/* Global: userland comparte el espacio de direcciones. */
static char mvar_value;

/* Estaticos: los argv de los hijos tienen que sobrevivir a mvar_main. */
static char  writer_letters[MAX_WRITERS][2];
static char *writer_argvs[MAX_WRITERS][1];
static char  reader_index[MAX_READERS][2];
static char *reader_argvs[MAX_READERS][1];

static const uint32_t reader_colors[MAX_READERS] = {
    0xFFFFFF, /* blanco   */
    0xFF5555, /* rojo     */
    0x55FF55, /* verde    */
    0x5555FF, /* azul     */
    0xFFFF55, /* amarillo */
    0xFF55FF, /* magenta  */
    0x55FFFF, /* cyan     */
};

static uint64_t spins_per_tick;

static void spin(uint64_t iterations) {
    for (volatile uint64_t i = 0; i < iterations; i++)
        ;
}

/* Cuenta cuantas iteraciones de spin() entran en un tick. */
static void calibrate_spin(void) {
    sys_yield();

    uint64_t tick = sys_ticks();
    while (sys_ticks() == tick)
        ;

    tick = sys_ticks();
    spins_per_tick = 0;
    while (sys_ticks() == tick) {
        spin(CALIBRATION_SPINS);
        spins_per_tick += CALIBRATION_SPINS;
    }
}

/* Espera activa aleatoria: con mas prioridad, el proceso termina antes. */
static void random_wait(uint32_t min_ticks, uint32_t spread) {
    spin((min_ticks + GetUniform(spread)) * spins_per_tick);
}

/* argv[0] = letra del escritor. */
static void writer_main(int argc, char **argv) {
    if (argc < 1 || argv == 0 || argv[0] == 0)
        sys_exit(-1);
    char letter = argv[0][0];

    sys_yield(); // para no pisar el prompt de la shell

    while (1) {
        random_wait(WRITER_WAIT_MIN, WRITER_WAIT_SPREAD);

        if (sys_sem_wait(SEM_EMPTY) < 0)
            sys_exit(-1);
        mvar_value = letter;
        sys_sem_post(SEM_FULL);
    }
}

/* argv[0] = indice de color del lector. */
static void reader_main(int argc, char **argv) {
    if (argc < 1 || argv == 0 || argv[0] == 0)
        sys_exit(-1);
    uint32_t color = reader_colors[argv[0][0] - '0'];

    while (1) {
        random_wait(READER_WAIT_MIN, READER_WAIT_SPREAD);

        if (sys_sem_wait(SEM_FULL) < 0)
            sys_exit(-1);
        char c = mvar_value;
        if ((int64_t)sys_write_color(STDOUT, &c, 1, color) < 0)
            sys_exit(-1);
        sys_sem_post(SEM_EMPTY);
    }
}

/* Mata los escritores y lectores de una corrida anterior. */
static void kill_previous_run(void) {
    static ProcessInfo procs[MAX_PROCESSES];
    uint64_t count = sys_ps(procs, MAX_PROCESSES);

    for (uint64_t i = 0; i < count; i++) {
        if (strcmp(procs[i].name, "mvar_writer") == 0 ||
            strcmp(procs[i].name, "mvar_reader") == 0)
            sys_kill(procs[i].pid);
    }
}

/* mvar <escritores> <lectores> */
void mvar_main(int argc, char **argv) {
    if (argc < 2 || argv == 0 || argv[0] == 0 || argv[1] == 0) {
        printf("uso: mvar <escritores> <lectores>\n");
        sys_exit(-1);
    }

    int64_t writers = satoi(argv[0]);
    int64_t readers = satoi(argv[1]);

    if (writers < 1 || writers > MAX_WRITERS) {
        printf("mvar: escritores debe estar entre 1 y %d\n", MAX_WRITERS);
        sys_exit(-1);
    }
    if (readers < 1 || readers > MAX_READERS) {
        printf("mvar: lectores debe estar entre 1 y %d\n", MAX_READERS);
        sys_exit(-1);
    }

    kill_previous_run();

    /* Cierra los semaforos viejos para que arranquen con su valor inicial. */
    while (sys_sem_close(SEM_EMPTY) == 0)
        ;
    while (sys_sem_close(SEM_FULL) == 0)
        ;

    calibrate_spin();

    mvar_value = 0;
    if (!sys_sem_open(SEM_EMPTY, 1) || !sys_sem_open(SEM_FULL, 0)) {
        printf("mvar: ERROR abriendo semaforos\n");
        sys_exit(-1);
    }

    for (int i = 0; i < writers; i++) {
        writer_letters[i][0] = (char)('A' + i);
        writer_letters[i][1] = '\0';
        writer_argvs[i][0]   = writer_letters[i];
        if (sys_create_process("mvar_writer", (void *)writer_main,
                               1, writer_argvs[i], 0) <= 0) {
            printf("mvar: ERROR creando escritor\n");
            sys_exit(-1);
        }
    }

    for (int i = 0; i < readers; i++) {
        reader_index[i][0] = (char)('0' + i);
        reader_index[i][1] = '\0';
        reader_argvs[i][0] = reader_index[i];
        if (sys_create_process("mvar_reader", (void *)reader_main,
                               1, reader_argvs[i], 0) <= 0) {
            printf("mvar: ERROR creando lector\n");
            sys_exit(-1);
        }
    }

    sys_exit(0);
}
