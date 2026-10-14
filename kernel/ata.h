#ifndef NPT_DOS_ATA_H
#define NPT_DOS_ATA_H

#include <stdint.h>

#define ATA_MAX_DEVICES 4

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

#endif
