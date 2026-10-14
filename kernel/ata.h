#ifndef NPT_DOS_ATA_H
#define NPT_DOS_ATA_H

#include <stdint.h>

#define ATA_MAX_DEVICES 4

#define STORAGE_SECTOR_SIZE 512U
#define STORAGE_IDE_DEVICE_COUNT 4U
#define STORAGE_SATA_ID_BASE STORAGE_IDE_DEVICE_COUNT

typedef enum {
    STORAGE_DEVICE_IDE = 0,
    STORAGE_DEVICE_SATA = 1
} storage_device_kind_t;

typedef struct {
    uint32_t id;
    storage_device_kind_t kind;
    char model[41];
    uint64_t sector_count;
} storage_device_info_t;

typedef struct {
    int detected;
    char model[41];
    uint32_t lba28_sectors;
    uint32_t addressable_sectors;
} ata_device_info_t;

/*
 * Device order:
 * 0 = Primary Master
 * 1 = Primary Slave
 * 2 = Secondary Master
 * 3 = Secondary Slave
 */

int ata_initialize(void);
uint32_t ata_device_count(void);

int ata_get_device_info(ata_device_info_t *info);
int ata_get_device_info_at(uint32_t device,
                           ata_device_info_t *info);

int ata_read_sector(uint32_t lba, uint8_t *buffer);
int ata_write_sector(uint32_t lba, const uint8_t *buffer);

int ata_read_sector_at(uint32_t device,
                       uint32_t lba,
                       uint8_t *buffer);

int ata_write_sector_at(uint32_t device,
                        uint32_t lba,
                        const uint8_t *buffer);

/* Initialize and enumerate both supported storage paths. */
int storage_initialize(void);
uint32_t storage_device_count(void);
int storage_get_device_info_at(
    uint32_t index,
    storage_device_info_t *info
);

/* Read or write exactly one 512-byte sector. */
int storage_read_sector(
    uint32_t device_id,
    uint64_t lba,
    uint8_t *buffer
);

int storage_write_sector(
    uint32_t device_id,
    uint64_t lba,
    const uint8_t *buffer
);

#endif
