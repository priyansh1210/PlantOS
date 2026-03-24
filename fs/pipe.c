#include "fs/pipe.h"
#include "fs/vfs.h"
#include "task/task.h"
#include "task/signal.h"
#include <plantos/signal.h>
#include "lib/string.h"
#include "lib/printf.h"

static struct pipe pipes[MAX_PIPES];

static struct pipe *pipe_alloc(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].used) {
            memset(&pipes[i], 0, sizeof(struct pipe));
            pipes[i].used = true;
            return &pipes[i];
        }
    }
    return NULL;
}

int pipe_create(int *read_fd, int *write_fd) {
    struct pipe *p = pipe_alloc();
    if (!p) return -1;

    int rfd = vfs_alloc_fd();
    if (rfd < 0) {
        p->used = false;
        return -1;
    }

    int wfd = vfs_alloc_fd();
    if (wfd < 0) {
        struct vfs_fd *rf = vfs_get_fd(rfd);
        if (rf) rf->used = false;
        p->used = false;
        return -1;
    }

    struct vfs_fd *rf = vfs_get_fd(rfd);
    rf->pipe = p;
    rf->pipe_mode = PIPE_READ;

    struct vfs_fd *wf = vfs_get_fd(wfd);
    wf->pipe = p;
    wf->pipe_mode = PIPE_WRITE;

    p->readers = 1;
    p->writers = 1;

    *read_fd = rfd;
    *write_fd = wfd;
    return 0;
}

static int pipe_read(struct pipe *p, void *buf, size_t count) {
    while (p->count == 0) {
        if (p->writers == 0)
            return 0; /* EOF — no more writers */
        /* Block until data available */
        task_block(p);
        /* Yield — let timer IRQ switch us out */
        struct task *cur = task_current();
        if (cur->state == TASK_BLOCKED) {
            __asm__ volatile ("sti");
            while (cur->state == TASK_BLOCKED)
                __asm__ volatile ("hlt");
        }
        /* Check if woken by a signal */
        if (cur->pending_signals)
            return -1;
    }

    /* Copy data from circular buffer */
    size_t avail = p->count;
    if (count > avail) count = avail;

    size_t copied = 0;
    while (copied < count) {
        ((uint8_t *)buf)[copied] = p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
        copied++;
    }
    p->count -= (uint32_t)copied;

    /* Wake a blocked writer */
    task_wake_one(p);

    return (int)copied;
}

static int pipe_write(struct pipe *p, const void *buf, size_t count) {
    if (p->readers == 0) {
        /* Broken pipe — send SIGPIPE to writer */
        struct task *cur = task_current();
        if (cur->is_user)
            signal_send(cur->pid, SIGPIPE);
        return -1;
    }

    size_t written = 0;
    while (written < count) {
        while (p->count == PIPE_BUF_SIZE) {
            if (p->readers == 0) {
                struct task *cur = task_current();
                if (cur->is_user)
                    signal_send(cur->pid, SIGPIPE);
                return (written > 0) ? (int)written : -1;
            }
            /* Block until space available */
            task_block(p);
            struct task *cur = task_current();
            if (cur->state == TASK_BLOCKED) {
                __asm__ volatile ("sti");
                while (cur->state == TASK_BLOCKED)
                    __asm__ volatile ("hlt");
            }
        }

        p->buf[p->write_pos] = ((const uint8_t *)buf)[written];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
        p->count++;
        written++;
    }

    /* Wake a blocked reader */
    task_wake_one(p);

    return (int)written;
}

int pipe_fd_read(int fd, void *buf, size_t count) {
    struct vfs_fd *f = vfs_get_fd(fd);
    if (!f || !f->pipe || f->pipe_mode != PIPE_READ)
        return -2; /* Not a pipe read FD */
    return pipe_read(f->pipe, buf, count);
}

int pipe_fd_write(int fd, const void *buf, size_t count) {
    struct vfs_fd *f = vfs_get_fd(fd);
    if (!f || !f->pipe || f->pipe_mode != PIPE_WRITE)
        return -2; /* Not a pipe write FD */
    return pipe_write(f->pipe, buf, count);
}

int pipe_fd_close(int fd) {
    struct vfs_fd *f = vfs_get_fd(fd);
    if (!f || !f->pipe)
        return -2; /* Not a pipe FD */

    /* Respect refcount — only release pipe end when last ref closes */
    if (f->refcount > 1) {
        f->refcount--;
        return 0;
    }

    struct pipe *p = f->pipe;

    if (f->pipe_mode == PIPE_READ) {
        p->readers--;
        if (p->readers == 0)
            task_wake_all(p); /* Wake blocked writers */
    } else if (f->pipe_mode == PIPE_WRITE) {
        p->writers--;
        if (p->writers == 0)
            task_wake_all(p); /* Wake blocked readers (EOF) */
    }

    /* Free the pipe if no more references */
    if (p->readers == 0 && p->writers == 0)
        p->used = false;

    f->pipe = NULL;
    f->pipe_mode = 0;
    f->refcount = 0;
    f->used = false;
    return 0;
}
