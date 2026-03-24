#ifndef CPU_SPINLOCK_H
#define CPU_SPINLOCK_H

#include <plantos/types.h>

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_init(spinlock_t *lk) {
    lk->locked = 0;
}

static inline void spin_lock(spinlock_t *lk) {
    while (1) {
        uint32_t old = 1;
        __asm__ volatile (
            "lock xchgl %0, %1"
            : "=r"(old), "+m"(lk->locked)
            : "0"(old)
            : "memory"
        );
        if (old == 0) return;  /* Was unlocked, we acquired it */
        /* Spin with pause hint to reduce bus contention */
        while (lk->locked)
            __asm__ volatile ("pause");
    }
}

static inline void spin_unlock(spinlock_t *lk) {
    __asm__ volatile ("" ::: "memory");  /* Compiler barrier */
    lk->locked = 0;
}

static inline int spin_trylock(spinlock_t *lk) {
    uint32_t old = 1;
    __asm__ volatile (
        "lock xchgl %0, %1"
        : "=r"(old), "+m"(lk->locked)
        : "0"(old)
        : "memory"
    );
    return old == 0;  /* 1 if acquired, 0 if was already locked */
}

#endif
