#include "drivers/ata.h"
#include "cpu/ports.h"
#include "lib/printf.h"
#include "lib/string.h"

/* Primary ATA controller I/O ports */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECT_COUNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_SEL   0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

/* Status bits */
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

/* Commands */
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH      0xE7

static bool disk_present = false;
static uint32_t total_sectors = 0;
static char model[41];

static void ata_wait_bsy(void) {
    while (inb(ATA_STATUS) & ATA_SR_BSY)
        ;
}

static void ata_wait_drq(void) {
    while (!(inb(ATA_STATUS) & ATA_SR_DRQ))
        ;
}

static int ata_poll(void) {
    /* Read status 4 times (400ns delay) */
    for (int i = 0; i < 4; i++)
        inb(ATA_STATUS);

    ata_wait_bsy();

    uint8_t status = inb(ATA_STATUS);
    if (status & ATA_SR_ERR) return -1;
    if (!(status & ATA_SR_DRQ)) return -1;
    return 0;
}

void ata_init(void) {
    /* Select master drive */
    outb(ATA_DRIVE_SEL, 0xA0);

    /* Wait a bit */
    for (int i = 0; i < 4; i++)
        inb(ATA_STATUS);

    /* Zero out sector count and LBA ports */
    outb(ATA_SECT_COUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);

    /* Send IDENTIFY command */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);
    if (status == 0) {
        kprintf("[ATA] No drive detected on primary master\n");
        disk_present = false;
        return;
    }

    ata_wait_bsy();

    /* Check for non-ATA device */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        kprintf("[ATA] Non-ATA device detected (ATAPI/SATA?)\n");
        disk_present = false;
        return;
    }

    /* Wait for DRQ or ERR */
    while (1) {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            kprintf("[ATA] IDENTIFY failed with error\n");
            disk_present = false;
            return;
        }
        if (status & ATA_SR_DRQ) break;
    }

    /* Read 256 words of identify data */
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(ATA_DATA);
    }

    /* Extract total LBA28 sectors (words 60-61) */
    total_sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);

    /* Extract model string (words 27-46, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        model[i * 2]     = (char)(identify[27 + i] >> 8);
        model[i * 2 + 1] = (char)(identify[27 + i] & 0xFF);
    }
    model[40] = '\0';
    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && model[i] == ' '; i--)
        model[i] = '\0';

    disk_present = true;
    uint32_t size_mb = total_sectors / 2048;
    kprintf("[ATA] Drive: %s\n", model);
    kprintf("[ATA] Capacity: %u sectors (%u MB)\n", total_sectors, size_mb);
}

bool ata_is_present(void) {
    return disk_present;
}

uint32_t ata_get_sector_count(void) {
    return total_sectors;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!disk_present) return -1;
    if (count == 0) return -1;

    ata_wait_bsy();

    /* LBA28 addressing, master drive */
    outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECT_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *ptr = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_poll() < 0) return -1;

        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            ptr[i] = inw(ATA_DATA);
        }
        ptr += 256;
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!disk_present) return -1;
    if (count == 0) return -1;

    ata_wait_bsy();

    /* LBA28 addressing, master drive */
    outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECT_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *ptr = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_poll() < 0) return -1;

        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, ptr[i]);
        }
        ptr += 256;
    }

    /* Flush write cache */
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait_bsy();

    return 0;
}
