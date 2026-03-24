#include "drivers/pci.h"
#include "cpu/ports.h"
#include "lib/printf.h"

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

static struct pci_device devices[PCI_MAX_DEVICES];
static int device_count = 0;

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static void pci_scan_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t reg0 = pci_config_read(bus, slot, func, 0x00);
    uint16_t vendor = reg0 & 0xFFFF;
    uint16_t devid  = (reg0 >> 16) & 0xFFFF;

    if (vendor == 0xFFFF) return;
    if (device_count >= PCI_MAX_DEVICES) return;

    struct pci_device *dev = &devices[device_count];
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = devid;

    uint32_t reg2 = pci_config_read(bus, slot, func, 0x08);
    dev->class_code = (reg2 >> 24) & 0xFF;
    dev->subclass   = (reg2 >> 16) & 0xFF;
    dev->prog_if    = (reg2 >> 8)  & 0xFF;

    uint32_t reg3 = pci_config_read(bus, slot, func, 0x0C);
    dev->header_type = (reg3 >> 16) & 0xFF;

    /* Read BARs (only for header type 0) */
    if ((dev->header_type & 0x7F) == 0) {
        for (int i = 0; i < 6; i++) {
            dev->bar[i] = pci_config_read(bus, slot, func, 0x10 + i * 4);
        }
        uint32_t irq_reg = pci_config_read(bus, slot, func, 0x3C);
        dev->irq_line = irq_reg & 0xFF;
    }

    device_count++;

    kprintf("[PCI] %02x:%02x.%d  %04x:%04x  class=%02x:%02x",
            bus, slot, func, vendor, devid, dev->class_code, dev->subclass);

    /* Friendly class names */
    if (dev->class_code == 0x01 && dev->subclass == 0x01)
        kprintf(" (IDE controller)");
    else if (dev->class_code == 0x01 && dev->subclass == 0x06)
        kprintf(" (SATA controller)");
    else if (dev->class_code == 0x02 && dev->subclass == 0x00)
        kprintf(" (Ethernet)");
    else if (dev->class_code == 0x03 && dev->subclass == 0x00)
        kprintf(" (VGA)");
    else if (dev->class_code == 0x06 && dev->subclass == 0x00)
        kprintf(" (Host bridge)");
    else if (dev->class_code == 0x06 && dev->subclass == 0x01)
        kprintf(" (ISA bridge)");

    kprintf("\n");
}

void pci_init(void) {
    device_count = 0;
    kprintf("[PCI] Scanning bus...\n");

    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t reg0 = pci_config_read(bus, slot, 0, 0x00);
            if ((reg0 & 0xFFFF) == 0xFFFF) continue;

            pci_scan_device(bus, slot, 0);

            /* Check for multi-function device */
            uint32_t reg3 = pci_config_read(bus, slot, 0, 0x0C);
            if ((reg3 >> 16) & 0x80) {
                for (int func = 1; func < 8; func++) {
                    pci_scan_device(bus, slot, func);
                }
            }
        }
    }

    kprintf("[PCI] Found %d devices\n", device_count);
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].vendor_id == vendor && devices[i].device_id == device)
            return &devices[i];
    }
    return NULL;
}

struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].class_code == class_code && devices[i].subclass == subclass)
            return &devices[i];
    }
    return NULL;
}

int pci_get_device_count(void) {
    return device_count;
}

struct pci_device *pci_get_device(int index) {
    if (index < 0 || index >= device_count) return NULL;
    return &devices[index];
}
