#ifndef NPT_DOS_ATA_H
#define NPT_DOS_ATA_H

#include <stdint.h>

int ata_initialize(void);
int ata_read_sector(uint32_t lba, uint8_t *buffer);
int ata_write_sector(uint32_t lba, const uint8_t *buffer);

#endif
