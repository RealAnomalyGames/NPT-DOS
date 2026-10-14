
#ifndef NPT_DOS_PCI_H
#define NPT_DOS_PCI_H

#include <stdint.h>

#define PCI_MAX_AHCI_CONTROLLERS 16

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar5;
} pci_ahci_controller_t;

/* Enumerate PCI devices and record AHCI controllers. */
void pci_initialize(void);

/* Retrieve a discovered AHCI controller by zero-based index. */
uint32_t pci_ahci_controller_count(void);

int pci_get_ahci_controller(
    uint32_t index,
    pci_ahci_controller_t *controller
);

/* PCI configuration-space access. */
uint32_t pci_read_config32(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
);

uint16_t pci_read_config16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset
);

/* Writes only the specified 16-bit configuration register. */
void pci_write_config16(
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    uint8_t offset,
    uint16_t value
);

#endif
