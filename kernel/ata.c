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

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_FLUSH    0xE7

#define ATA_SR_ERR     0x01
#define ATA_SR_DRQ     0x08
#define ATA_SR_DF      0x20
#define ATA_SR_BSY     0x80

#define ATA_TIMEOUT    1000000U
#define ATA_MAX_LBA    0x0FFFFFFFU
#define ATA_MAX_SECTORS 0x10000000U

static int ata_ready = 0;
static ata_device_info_t ata_info;

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

/* Wait until the selected device is no longer busy. */
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

/* Wait for a command's data phase to finish. */
static int ata_wait_complete(void)
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

        if (!(status & ATA_SR_DRQ)) {
            return 1;
        }
    }

    return 0;
}

/*
 * Decode ATA IDENTIFY words 27-46.
 * ATA stores two model characters per word, with the high byte first
 * in the logical model string.
 */
static void ata_decode_model(const uint16_t *identify)
{
    uint32_t i;
    uint32_t length = 40;

    for (i = 0; i < 20; i++) {
        uint16_t word = identify[27 + i];

        ata_info.model[i * 2] = (char)(word >> 8);
        ata_info.model[i * 2 + 1] = (char)(word & 0xFF);
    }

    ata_info.model[40] = '\0';

    /* Remove trailing spaces from the fixed-width model field. */
    while (length > 0 && ata_info.model[length - 1] == ' ') {
        length--;
    }

    ata_info.model[length] = '\0';
}

/* Cache the IDENTIFY information needed by NPT-DOS. */
static void ata_decode_identify(const uint16_t *identify)
{
    uint32_t sectors;

    ata_decode_model(identify);

    sectors = (uint32_t)identify[60]
            | ((uint32_t)identify[61] << 16);

    ata_info.lba28_sectors = sectors;

    if (sectors > ATA_MAX_SECTORS) {
        sectors = ATA_MAX_SECTORS;
    }

    ata_info.addressable_sectors = sectors;
    ata_info.detected = 1;
}

/*
 * Initialize the primary-master ATA device and cache its IDENTIFY data.
 * This function reads device information but does not write disk sectors.
 */
int ata_initialize(void)
{
    uint32_t timeout;
    uint8_t status;
    uint32_t i;
    uint16_t identify[256];

    ata_ready = 0;
    ata_info.detected = 0;
    ata_info.model[0] = '\0';
    ata_info.lba28_sectors = 0;
    ata_info.addressable_sectors = 0;

    /* Select the primary master for IDENTIFY. */
    outb(ATA_DRIVE, 0xA0);
    ata_delay_400ns();

    status = inb(ATA_STATUS);

    if (status == 0x00 || status == 0xFF) {
        return 0;
    }

    /* Wait for the selected device to finish any pending operation. */
    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        status = inb(ATA_STATUS);

        if (status == 0x00 || status == 0xFF) {
            return 0;
        }

        if (!(status & ATA_SR_BSY)) {
            break;
        }
    }

    if (timeout == ATA_TIMEOUT) {
        return 0;
    }

    /* IDENTIFY requires these registers to be zero. */
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);

    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay_400ns();

    status = inb(ATA_STATUS);

    if (status == 0x00 || status == 0xFF) {
        return 0;
    }

    /* Wait for IDENTIFY to finish its busy phase. */
    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        status = inb(ATA_STATUS);

        if (status == 0x00 || status == 0xFF) {
            return 0;
        }

        if (!(status & ATA_SR_BSY)) {
            break;
        }
    }

    if (timeout == ATA_TIMEOUT) {
        return 0;
    }

    /* Nonzero LBA1/LBA2 indicates a non-ATA device signature. */
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0) {
        return 0;
    }

    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
        return 0;
    }

    if (!(status & ATA_SR_DRQ)) {
        return 0;
    }

    /* Save all 256 words from the 512-byte IDENTIFY response. */
    for (i = 0; i < 256; i++) {
        identify[i] = inw(ATA_DATA);
    }

    ata_delay_400ns();

    if (!ata_wait_complete()) {
        return 0;
    }

    /* Select the master again in 28-bit LBA mode. */
    outb(ATA_DRIVE, 0xE0);
    ata_delay_400ns();

    if (!ata_wait(0)) {
        return 0;
    }

    ata_decode_identify(identify);

    ata_ready = 1;
    return 1;
}

/*
 * Copy cached device information to the caller.
 * Returns 1 when information is available, otherwise 0.
 */
int ata_get_device_info(ata_device_info_t *info)
{
    uint32_t i;

    if (info == 0 || !ata_ready || !ata_info.detected) {
        return 0;
    }

    info->detected = ata_info.detected;
    info->lba28_sectors = ata_info.lba28_sectors;
    info->addressable_sectors = ata_info.addressable_sectors;

    for (i = 0; i < sizeof(info->model); i++) {
        info->model[i] = ata_info.model[i];
    }

    return 1;
}

static int ata_prepare(uint32_t lba, uint8_t command)
{
    if (!ata_ready || lba > ATA_MAX_LBA) {
        return 0;
    }

    outb(ATA_DRIVE,
         (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
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

    if (buffer == 0 || !ata_prepare(lba, ATA_CMD_READ)) {
        return 0;
    }

    for (i = 0; i < 256; i++) {
        uint16_t word = inw(ATA_DATA);

        buffer[i * 2] = (uint8_t)(word & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    ata_delay_400ns();

    return ata_wait_complete();
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    uint32_t i;

    if (buffer == 0 || !ata_prepare(lba, ATA_CMD_WRITE)) {
        return 0;
    }

    for (i = 0; i < 256; i++) {
        uint16_t word =
            (uint16_t)buffer[i * 2]
            | ((uint16_t)buffer[i * 2 + 1] << 8);

        outw(ATA_DATA, word);
    }

    ata_delay_400ns();

    if (!ata_wait_complete()) {
        return 0;
    }

    outb(ATA_COMMAND, ATA_CMD_FLUSH);

    return ata_wait_complete();
}
