#ifndef FS_PIPE_H
#define FS_PIPE_H

#include <plantos/types.h>

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES     16

/* Pipe modes for FDs */
#define PIPE_READ  1
#define PIPE_WRITE 2

struct pipe {
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;      /* bytes currently in buffer */
    uint32_t readers;    /* number of open read ends */
    uint32_t writers;    /* number of open write ends */
    bool     used;
};

/* Create a pipe, returning read/write FDs */
int pipe_create(int *read_fd, int *write_fd);

/* Read from a pipe FD. Returns -2 if fd is not a pipe. */
int pipe_fd_read(int fd, void *buf, size_t count);

/* Write to a pipe FD. Returns -2 if fd is not a pipe. */
int pipe_fd_write(int fd, const void *buf, size_t count);

/* Close a pipe FD. Returns -2 if fd is not a pipe. */
int pipe_fd_close(int fd);

#endif
