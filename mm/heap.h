#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <plantos/types.h>

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void  heap_dump_stats(void);

#endif
