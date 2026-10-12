#ifndef NPT_DOS_ATA_H
#define NPT_DOS_ATA_H

#include <stdint.h>

typedef struct {
    int detected;
    char model[41];
    uint32_t lba28_sectors;
    uint32_t addressable_sectors;
} ata_device_info_t;

int ata_initialize(void);
int ata_read_sector(uint32_t lba, uint8_t *buffer);
int ata_write_sector(uint32_t lba, const uint8_t *buffer);

int ata_get_device_info(ata_device_info_t *info);

#endif
