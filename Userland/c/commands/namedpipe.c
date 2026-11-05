#include <stdint.h>
#include "lib/userlib.h"
#include "commands/commands.h"

/* nread y nwrite: dos procesos sin relacion que se comunican por un pipe nombrado.
** El unico dato que comparten es el nombre, que acuerdan a priori. */

#define COPY_CHUNK 128

/* nread <nombre>: copia a stdout lo que llega al pipe hasta EOF. */
void nread_main(int argc, char **argv) {
    if (argc < 1 || argv == 0 || argv[0] == 0) {
        print("uso: nread <nombre>\n");
        sys_exit(-1);
    }

    int64_t fd = sys_pipe_open(argv[0], PIPE_READ);
    if (fd < 0) {
        print("nread: no se pudo abrir el pipe\n");
        sys_exit(-1);
    }

    char buf[COPY_CHUNK];
    int64_t n;
    while ((n = (int64_t)sys_read((uint64_t)fd, buf, sizeof(buf))) > 0) {
        if ((int64_t)sys_write(STDOUT, buf, (uint64_t)n) < 0)
            sys_exit(-1);
    }

    sys_pipe_close((int)fd);
    sys_exit(n < 0 ? -1 : 0);
}

/* Escribe todo buf en el pipe. Termina el proceso si ya no quedan lectores. */
static void write_or_exit(int64_t fd, const char *buf, uint64_t len) {
    if ((int64_t)sys_write((uint64_t)fd, buf, len) < 0) {
        print("nwrite: no quedan lectores\n");
        sys_exit(-1);
    }
}

/* nwrite <nombre> [texto...]: escribe el texto en el pipe y, si no hay texto, copia
** stdin hasta EOF. */
void nwrite_main(int argc, char **argv) {
    if (argc < 1 || argv == 0 || argv[0] == 0) {
        print("uso: nwrite <nombre> [texto]\n");
        sys_exit(-1);
    }

    int64_t fd = sys_pipe_open(argv[0], PIPE_WRITE);
    if (fd < 0) {
        print("nwrite: no se pudo abrir el pipe\n");
        sys_exit(-1);
    }

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            write_or_exit(fd, argv[i], strlen(argv[i]));
            write_or_exit(fd, (i < argc - 1) ? " " : "\n", 1);
        }
    } else {
        char buf[COPY_CHUNK];
        uint64_t n;
        while ((n = read_full(buf, sizeof(buf))) > 0)
            write_or_exit(fd, buf, n);
    }

    sys_pipe_close((int)fd);
    sys_exit(0);
}
