#include "tests/test_pipe.h"
#include "tests/test_util.h"
#include "lib/userlib.h"
#include "lib/io.h"
#include "lib/format.h"

/* test_pipe: un productor y un consumidor conectados por un pipe, como "a | b".
** El consumidor verifica orden, integridad y cantidad de bytes. Con mas de
** 4096 bytes se llena el buffer del pipe y se prueba el bloqueo. */

#define DEFAULT_BYTES 16384
#define IO_CHUNK      512

/* bits 0-7: fg, bits 16-31: fd_in, bits 32-47: fd_out. */
static uint64_t pack_fds(uint8_t fg, int fd_in, int fd_out) {
    return (uint64_t)fg
         | ((uint64_t)(uint16_t)fd_in  << 16)
         | ((uint64_t)(uint16_t)fd_out << 32);
}

/* Escribe argv[0] bytes (el byte i vale i & 0xFF) en STDOUT, que es el pipe. */
static void pipe_writer(int argc, char *argv[]) {
    if (argc < 1 || argv == 0 || argv[0] == 0)
        sys_exit(-1);

    uint64_t total = (uint64_t)satoi(argv[0]);
    char     buf[IO_CHUNK];
    uint64_t sent = 0;

    while (sent < total) {
        uint64_t chunk = 0;
        while (chunk < IO_CHUNK && sent < total)
            buf[chunk++] = (char)(sent++ & 0xFF);

        uint64_t off = 0;
        while (off < chunk) {
            int64_t w = (int64_t)sys_write(STDOUT, buf + off, chunk - off);
            if (w <= 0)
                sys_exit(-1);
            off += (uint64_t)w;
        }
    }
    sys_exit(0);
}

/* Lee de STDIN hasta EOF y compara contra el patron y la cantidad esperada. */
static void pipe_reader(int argc, char *argv[]) {
    if (argc < 1 || argv == 0 || argv[0] == 0)
        sys_exit(-1);

    uint64_t expected     = (uint64_t)satoi(argv[0]);
    char     buf[IO_CHUNK];
    uint64_t got          = 0;
    uint8_t  integrity_ok = 1;
    uint64_t n;

    while ((n = read_full(buf, IO_CHUNK)) > 0) {
        for (uint64_t i = 0; i < n; i++) {
            if ((uint8_t)buf[i] != (uint8_t)(got & 0xFF))
                integrity_ok = 0;
            got++;
        }
    }

    printf("  [reader] recibidos %d/%d bytes, integridad %s\n",
           (int)got, (int)expected, integrity_ok ? "OK" : "FALLIDA");

    sys_exit((integrity_ok && got == expected) ? 0 : -1);
}

static int64_t test_pipe_internal(uint64_t total) {
    int fds[2];
    if (sys_pipe(fds) < 0) {
        printf("test_pipe: ERROR creando el pipe\n");
        return -1;
    }

    char num[24];
    num_to_str(total, num, 10);
    char *args[1] = { num };

    int64_t pid_w = sys_create_process_fd("pipe_writer", (void *)pipe_writer,
                                          1, args, pack_fds(1, 0, fds[1]));
    int64_t pid_r = sys_create_process_fd("pipe_reader", (void *)pipe_reader,
                                          1, args, pack_fds(1, fds[0], STDOUT));

    /* El padre cierra sus extremos para que el lector reciba EOF. */
    sys_pipe_close(fds[0]);
    sys_pipe_close(fds[1]);

    if (pid_w <= 0 || pid_r <= 0) {
        printf("test_pipe: ERROR creando los procesos del pipe\n");
        if (pid_w > 0) sys_kill((uint64_t)pid_w);
        if (pid_r > 0) sys_kill((uint64_t)pid_r);
        return -1;
    }

    int64_t r_status = sys_waitpid((uint64_t)pid_r);
    int64_t w_status = sys_waitpid((uint64_t)pid_w);

    if (r_status == 0 && w_status == 0) {
        printf("test_pipe OK\n");
        return 0;
    }
    printf("test_pipe ERROR (writer=%d, reader=%d)\n",
           (int)w_status, (int)r_status);
    return -1;
}

/* test_pipe [bytes] */
void test_pipe_main(int argc, char **argv) {
    uint64_t total = DEFAULT_BYTES;
    if (argc >= 1 && argv != 0 && argv[0] != 0) {
        int64_t v = satoi(argv[0]);
        if (v > 0)
            total = (uint64_t)v;
    }

    printf("test_pipe: transfiriendo %d bytes por un pipe (productor->consumidor)...\n",
           (int)total);
    test_pipe_internal(total);
    sys_exit(0);
}
