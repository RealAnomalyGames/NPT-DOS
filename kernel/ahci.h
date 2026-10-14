#ifndef NPT_DOS_AHCI_H
#define NPT_DOS_AHCI_H

#include <stdint.h>

#define AHCI_MAX_CONTROLLERS 16
#define AHCI_MAX_PORTS       32
#define AHCI_SECTOR_SIZE     512U
#define AHCI_MODEL_LENGTH    41

typedef enum {
    AHCI_DEVICE_NONE = 0,
    AHCI_DEVICE_SATA,
    AHCI_DEVICE_ATAPI,
    AHCI_DEVICE_SEMB,
    AHCI_DEVICE_PORT_MULTIPLIER
} ahci_device_type_t;

typedef struct {
    int detected;
    uint8_t port_number;
    uint8_t device_type;
    uint8_t sata_signature;
    uint32_t sata_status;
    uint32_t task_file_data;

    /* Populated only after successful IDENTIFY DEVICE. */
    int identified;
    char model[AHCI_MODEL_LENGTH];
    uint64_t sector_count;
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

/* Controller discovery and existing read-only inspection. */
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

/* Disk identification and sector reads. */
int ahci_identify_device(
    uint32_t controller_index,
    uint32_t port_number
);

int ahci_read_sector(
    uint32_t controller_index,
    uint32_t port_number,
    uint64_t lba,
    uint8_t *buffer
);

int ahci_write_sector(
    uint32_t controller,
    uint32_t port,
    uint64_t lba,
    const uint8_t *buffer
);

int ahci_flush_cache(
    uint32_t controller,
    uint32_t port
);

/*
 * Sector writes will be exposed only after DMA reads, controller
 * initialization, and read-back verification have been validated.
 */

#endif
