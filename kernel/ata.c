#include <stdint.h>
#include "ata.h"

#define ATA_PRIMARY_IO       0x1F0
#define ATA_PRIMARY_CTRL     0x3F6
#define ATA_SECONDARY_IO     0x170
#define ATA_SECONDARY_CTRL   0x376

#define ATA_REG_DATA         0
#define ATA_REG_ERROR        1
#define ATA_REG_SECCOUNT     2
#define ATA_REG_LBA0         3
#define ATA_REG_LBA1         4
#define ATA_REG_LBA2         5
#define ATA_REG_DRIVE        6
#define ATA_REG_STATUS       7
#define ATA_REG_COMMAND      7

#define ATA_CMD_READ         0x20
#define ATA_CMD_WRITE        0x30
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_FLUSH        0xE7

#define ATA_SR_ERR           0x01
#define ATA_SR_DRQ           0x08
#define ATA_SR_DF            0x20
#define ATA_SR_BSY           0x80

#define ATA_TIMEOUT          1000000U
#define ATA_MAX_LBA          0x0FFFFFFFU
#define ATA_MAX_SECTORS      0x10000000U

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t slave;
} ata_device_t;

static const ata_device_t ata_devices[ATA_MAX_DEVICES] = {
    { ATA_PRIMARY_IO,   ATA_PRIMARY_CTRL,   0 },
    { ATA_PRIMARY_IO,   ATA_PRIMARY_CTRL,   1 },
    { ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 0 },
    { ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 1 }
};

static ata_device_info_t ata_info[ATA_MAX_DEVICES];
static uint32_t ata_detected_count;
static int ata_ready;

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

static uint8_t ata_read_reg(const ata_device_t *dev, uint8_t reg)
{
    return inb((uint16_t)(dev->io_base + reg));
}

static void ata_write_reg(
    const ata_device_t *dev,
    uint8_t reg,
    uint8_t value)
{
    outb((uint16_t)(dev->io_base + reg), value);
}

static void ata_delay_400ns(const ata_device_t *dev)
{
    (void)inb(dev->ctrl_base);
    (void)inb(dev->ctrl_base);
    (void)inb(dev->ctrl_base);
    (void)inb(dev->ctrl_base);
}

/*
 * Wait for BSY to clear.
 * If require_drq is nonzero, also wait for DRQ.
 */
static int ata_wait(
    const ata_device_t *dev,
    int require_drq)
{
    uint32_t timeout;

    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        uint8_t status =
            ata_read_reg(dev, ATA_REG_STATUS);

        if (status == 0x00 || status == 0xFF)
            return 0;

        if (status & ATA_SR_BSY)
            continue;

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return 0;

        if (!require_drq || (status & ATA_SR_DRQ))
            return 1;
    }

    return 0;
}

/* Wait until a command's data phase is complete. */
static int ata_wait_complete(const ata_device_t *dev)
{
    uint32_t timeout;

    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        uint8_t status =
            ata_read_reg(dev, ATA_REG_STATUS);

        if (status == 0x00 || status == 0xFF)
            return 0;

        if (status & ATA_SR_BSY)
            continue;

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return 0;

        if (!(status & ATA_SR_DRQ))
            return 1;
    }

    return 0;
}

static void ata_decode_model(
    const uint16_t *identify,
    ata_device_info_t *info)
{
    uint32_t i;
    uint32_t length = 40;

    for (i = 0; i < 20; i++) {
        uint16_t word = identify[27 + i];

        info->model[i * 2] =
            (char)(word >> 8);

        info->model[i * 2 + 1] =
            (char)(word & 0xFF);
    }

    info->model[40] = '\0';

    while (length > 0 &&
           info->model[length - 1] == ' ') {
        length--;
    }

    info->model[length] = '\0';
}

static void ata_decode_identify(
    uint32_t device,
    const uint16_t *identify)
{
    uint32_t sectors;
    ata_device_info_t *info = &ata_info[device];

    ata_decode_model(identify, info);

    sectors = (uint32_t)identify[60]
            | ((uint32_t)identify[61] << 16);

    info->lba28_sectors = sectors;

    if (sectors > ATA_MAX_SECTORS)
        sectors = ATA_MAX_SECTORS;

    info->addressable_sectors = sectors;
    info->detected = 1;
}

/*
 * Probe one legacy IDE position using IDENTIFY DEVICE.
 * This routine does not write any disk sectors.
 */
static int ata_probe_device(uint32_t device)
{
    const ata_device_t *dev = &ata_devices[device];
    uint16_t identify[256];
    uint32_t i;
    uint32_t timeout;
    uint8_t status;

    /*
     * Select master/slave. Bit 7 is set for the ATA
     * device/head register selection pattern.
     */
    ata_write_reg(
        dev,
        ATA_REG_DRIVE,
        (uint8_t)(0xA0 | (dev->slave << 4)));

    ata_delay_400ns(dev);

    status = ata_read_reg(dev, ATA_REG_STATUS);

    if (status == 0x00 || status == 0xFF)
        return 0;

    if (!ata_wait(dev, 0))
        return 0;

    /* IDENTIFY DEVICE requires zero in these registers. */
    ata_write_reg(dev, ATA_REG_SECCOUNT, 0);
    ata_write_reg(dev, ATA_REG_LBA0, 0);
    ata_write_reg(dev, ATA_REG_LBA1, 0);
    ata_write_reg(dev, ATA_REG_LBA2, 0);

    ata_write_reg(dev, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay_400ns(dev);

    status = ata_read_reg(dev, ATA_REG_STATUS);

    if (status == 0x00 || status == 0xFF)
        return 0;

    /*
     * IDENTIFY may initially report BSY.
     * Wait for a data phase, rejecting devices that
     * report errors or never provide identification data.
     */
    for (timeout = 0; timeout < ATA_TIMEOUT; timeout++) {
        status = ata_read_reg(dev, ATA_REG_STATUS);

        if (status == 0x00 || status == 0xFF)
            return 0;

        if (status & ATA_SR_BSY)
            continue;

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return 0;

        /*
         * ATAPI devices have a different IDENTIFY command.
         * This initial driver supports ATA disks only.
         */
        if (ata_read_reg(dev, ATA_REG_LBA1) != 0 ||
            ata_read_reg(dev, ATA_REG_LBA2) != 0)
            return 0;

        if (!(status & ATA_SR_DRQ))
            return 0;

        break;
    }

    if (timeout == ATA_TIMEOUT)
        return 0;

    for (i = 0; i < 256; i++)
        identify[i] = inw(dev->io_base + ATA_REG_DATA);

    ata_delay_400ns(dev);

    if (!ata_wait_complete(dev))
        return 0;

    ata_decode_identify(device, identify);
    return 1;
}

/*
 * Probe all four legacy IDE positions.
 * Returns 1 if at least one ATA device is detected.
 */
int ata_initialize(void)
{
    uint32_t i;

    ata_ready = 0;
    ata_detected_count = 0;

    for (i = 0; i < ATA_MAX_DEVICES; i++) {
        ata_info[i].detected = 0;
        ata_info[i].model[0] = '\0';
        ata_info[i].lba28_sectors = 0;
        ata_info[i].addressable_sectors = 0;
    }

    for (i = 0; i < ATA_MAX_DEVICES; i++) {
        if (ata_probe_device(i))
            ata_detected_count++;
    }

    ata_ready = (ata_detected_count != 0);
    return ata_ready;
}

uint32_t ata_device_count(void)
{
    return ata_detected_count;
}

int ata_get_device_info_at(
    uint32_t device,
    ata_device_info_t *info)
{
    uint32_t i;

    if (!ata_ready || info == 0 ||
        device >= ATA_MAX_DEVICES ||
        !ata_info[device].detected) {
        return 0;
    }

    info->detected = ata_info[device].detected;
    info->lba28_sectors =
        ata_info[device].lba28_sectors;
    info->addressable_sectors =
        ata_info[device].addressable_sectors;

    for (i = 0; i < sizeof(info->model); i++)
        info->model[i] = ata_info[device].model[i];

    return 1;
}

/* Backward-compatible primary-master information API. */
int ata_get_device_info(ata_device_info_t *info)
{
    return ata_get_device_info_at(0, info);
}

static int ata_prepare(
    uint32_t device,
    uint32_t lba,
    uint8_t command)
{
    const ata_device_t *dev;

    if (!ata_ready ||
        device >= ATA_MAX_DEVICES ||
        !ata_info[device].detected ||
        lba > ATA_MAX_LBA ||
        lba >= ata_info[device].addressable_sectors) {
        return 0;
    }

    dev = &ata_devices[device];

    ata_write_reg(
        dev,
        ATA_REG_DRIVE,
        (uint8_t)(
            0xE0 |
            (dev->slave << 4) |
            ((lba >> 24) & 0x0F)));

    ata_delay_400ns(dev);

    if (!ata_wait(dev, 0))
        return 0;

    ata_write_reg(dev, ATA_REG_SECCOUNT, 1);
    ata_write_reg(dev, ATA_REG_LBA0, (uint8_t)lba);
    ata_write_reg(dev, ATA_REG_LBA1, (uint8_t)(lba >> 8));
    ata_write_reg(dev, ATA_REG_LBA2, (uint8_t)(lba >> 16));

    ata_write_reg(dev, ATA_REG_COMMAND, command);

    return ata_wait(dev, 1);
}

int ata_read_sector_at(
    uint32_t device,
    uint32_t lba,
    uint8_t *buffer)
{
    const ata_device_t *dev;
    uint32_t i;

    if (buffer == 0 ||
        !ata_prepare(device, lba, ATA_CMD_READ)) {
        return 0;
    }

    dev = &ata_devices[device];

    for (i = 0; i < 256; i++) {
        uint16_t word = inw(dev->io_base + ATA_REG_DATA);

        buffer[i * 2] = (uint8_t)word;
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    ata_delay_400ns(dev);

    return ata_wait_complete(dev);
}

int ata_write_sector_at(
    uint32_t device,
    uint32_t lba,
    const uint8_t *buffer)
{
    const ata_device_t *dev;
    uint32_t i;

    if (buffer == 0 ||
        !ata_prepare(device, lba, ATA_CMD_WRITE)) {
        return 0;
    }

    dev = &ata_devices[device];

    for (i = 0; i < 256; i++) {
        uint16_t word =
            (uint16_t)buffer[i * 2] |
            ((uint16_t)buffer[i * 2 + 1] << 8);

        outw(dev->io_base + ATA_REG_DATA, word);
    }

    if (!ata_wait_complete(dev))
        return 0;

    ata_write_reg(dev, ATA_REG_COMMAND, ATA_CMD_FLUSH);

    return ata_wait_complete(dev);
}

/* Preserve existing callers as primary-master operations. */
int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    return ata_read_sector_at(0, lba, buffer);
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    return ata_write_sector_at(0, lba, buffer);
}
