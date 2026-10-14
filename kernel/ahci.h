
#ifndef NPT_DOS_AHCI_H
#define NPT_DOS_AHCI_H

#include <stdint.h>

#define AHCI_MAX_CONTROLLERS 16
#define AHCI_MAX_PORTS       32

typedef struct {
    int detected;
    uint8_t port_number;
    uint8_t device_type;
    uint8_t sata_signature;
    uint32_t sata_status;
    uint32_t task_file_data;
} ahci_port_info_t;

typedef struct {
    int detected;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t abar;
    uint32_t capabilities;
    uint32_t implemented_ports;
    uint32_t version;
} ahci_controller_info_t;

/*
 * Discover AHCI controllers and inspect their implemented ports.
 * This initial stage performs no disk commands or disk writes.
 */
int ahci_initialize(void);

uint32_t ahci_controller_count(void);

int ahci_get_controller_info(
    uint32_t index,
    ahci_controller_info_t *info
);

int ahci_get_port_info(
    uint32_t controller_index,
    uint32_t port_number,
    ahci_port_info_t *info
);

#endif
