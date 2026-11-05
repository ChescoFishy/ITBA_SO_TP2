#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_FD_READ_BASE  100
#define PIPE_FD_WRITE_BASE 200
#define MAX_PIPES          16
#define PIPE_BUF_SIZE      4096
#define PIPE_NAME_LEN      16

/* Extremo que se abre con pipe_open. */
#define PIPE_OPEN_READ     0
#define PIPE_OPEN_WRITE    1

void    pipe_init(void);
int     pipe_create(int fds[2]);
int     pipe_open(const char *name, int mode);
void    pipe_wait_peer(int fd);
int     pipe_close(int fd);
int64_t pipe_read(int fd, char *buf, uint64_t n);
int64_t pipe_write(int fd, const char *buf, uint64_t n);
int     pipe_is_fd(int fd);
/* Incrementa readers/writers cuando un nuevo proceso hereda un extremo del pipe
** (fork-style). El close correspondiente lo decrementa. */
int     pipe_inherit_fd(int fd);

#endif
