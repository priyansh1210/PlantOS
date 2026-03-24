#ifndef DRIVERS_ATA_H
#define DRIVERS_ATA_H

#include <plantos/types.h>

#define ATA_SECTOR_SIZE 512

void ata_init(void);
bool ata_is_present(void);

/* Read/write sectors using LBA28 PIO */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);

/* Disk info */
uint32_t ata_get_sector_count(void);

#endif
