#include <stdint.h>
#include "ata.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALTSTATUS  0x3F6
#define ATA_CONTROL    0x3F6

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_FLUSH  0xE7

#define ATA_SR_ERR     0x01
#define ATA_SR_DRQ     0x08
#define ATA_SR_DF      0x20
#define ATA_SR_BSY     0x80

#define ATA_TIMEOUT    1000000U
#define ATA_MAX_LBA    0x0FFFFFFFU

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void ata_delay_400ns(void)
{
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
}

/* Wait until the drive is not busy; optionally require data readiness. */
static int ata_wait(int require_drq)
{
    uint32_t timeout;

    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0x00 || status == 0xFF) {
            return 0;
        }

        if (status & ATA_SR_BSY) {
            continue;
        }

        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return 0;
        }

        if (!require_drq || (status & ATA_SR_DRQ)) {
            return 1;
        }
    }

    return 0;
}

int ata_initialize(void)
{
    uint8_t status = inb(ATA_STATUS);

    if (status == 0x00 || status == 0xFF) {
        return 0;
    }

    outb(ATA_CONTROL, 0x00);
    ata_delay_400ns();

    return ata_wait(0);
}

static int ata_prepare(uint32_t lba, uint8_t command)
{
    if (lba > ATA_MAX_LBA) {
        return 0;
    }

    if (!ata_wait(0)) {
        return 0;
    }

    /* Select primary master and use 28-bit LBA addressing. */
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();

    if (!ata_wait(0)) {
        return 0;
    }

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, command);

    return ata_wait(1);
}

int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    uint32_t i;
    uint16_t word;

    if (buffer == 0 || !ata_prepare(lba, ATA_CMD_READ)) {
        return 0;
    }

    for (i = 0; i < 256; i++) {
        word = inw(ATA_DATA);
        buffer[i * 2] = (uint8_t)(word & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    ata_delay_400ns();

    return ata_wait(0);
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    uint32_t i;
    uint16_t word;

    if (buffer == 0 || !ata_prepare(lba, ATA_CMD_WRITE)) {
        return 0;
    }

    for (i = 0; i < 256; i++) {
        word = (uint16_t)buffer[i * 2]
             | ((uint16_t)buffer[i * 2 + 1] << 8);
        outw(ATA_DATA, word);
    }

    ata_delay_400ns();

    if (!ata_wait(0)) {
        return 0;
    }

    outb(ATA_COMMAND, ATA_CMD_FLUSH);

    return ata_wait(0);
}
