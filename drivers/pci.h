#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <plantos/types.h>

struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq_line;
};

#define PCI_MAX_DEVICES 32

void pci_init(void);
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
struct pci_device *pci_find_device(uint16_t vendor, uint16_t device);
struct pci_device *pci_find_class(uint8_t class_code, uint8_t subclass);
int pci_get_device_count(void);
struct pci_device *pci_get_device(int index);

#endif
