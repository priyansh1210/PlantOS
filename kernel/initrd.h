#ifndef KERNEL_INITRD_H
#define KERNEL_INITRD_H

/* Embed compiled ELF binaries into ramfs at boot */
void initrd_init(void);

#endif
