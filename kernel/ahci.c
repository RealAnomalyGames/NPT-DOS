#include <stdint.h>
#include <stddef.h>

#include "ahci.h"
#include "pci.h"
#include "terminal.h"

/* PCI configuration */
#define PCI_COMMAND_REGISTER  0x04
#define PCI_COMMAND_MEMORY    0x0002
#define PCI_COMMAND_BUSMASTER 0x0004

/* HBA registers */
#define HBA_CAP       0x00
#define HBA_GHC       0x04
#define HBA_IS        0x08
#define HBA_PI        0x0C
#define HBA_VS        0x10
#define HBA_CAP2      0x24
#define HBA_BOHC      0x28
#define HBA_PORT_BASE 0x100
#define HBA_PORT_SIZE 0x80

/* Port registers */
#define PX_CLB   0x00
#define PX_CLBU  0x04
#define PX_FB    0x08
#define PX_FBU   0x0C
#define PX_IS    0x10
#define PX_IE    0x14
#define PX_CMD   0x18
#define PX_TFD   0x20
#define PX_SIG   0x24
#define PX_SSTS  0x28
#define PX_SCTL  0x2C
#define PX_SERR  0x30
#define PX_SACT  0x34
#define PX_CI    0x38

/* Port command bits */
#define PX_CMD_ST   (1u << 0)
#define PX_CMD_FRE  (1u << 4)
#define PX_CMD_FR   (1u << 14)
#define PX_CMD_CR   (1u << 15)

/* Task-file status bits */
#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_BSY  0x80

/* Interrupt status */
#define PX_IS_TFES (1u << 30)

/* SATA status */
#define SATA_DET_MASK 0x0F
#define SATA_DET_READY 0x03
#define SATA_IPM_MASK 0x0F00
#define SATA_IPM_ACTIVE 0x0100

/* AHCI signatures */
#define SATA_SIG_ATA  0x00000101u
#define SATA_SIG_ATAPI 0xEB140101u
#define SATA_SIG_SEMB 0xC33C0101u
#define SATA_SIG_PM   0x96690101u

/* HBA global host control */
#define HBA_GHC_AE (1u << 31)

/* ATA commands */
#define ATA_CMD_IDENTIFY_DEVICE 0xEC
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT  0x35
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA

/* AHCI command-header write direction. */
#define AHCI_CMD_HEADER_WRITE (1u << 6)

#define AHCI_MMIO_REQUIRED_SIZE 0x1100u
#define AHCI_POLL_LIMIT         10000000u

/*
 * No paging is enabled by this kernel. Static object addresses therefore
 * match physical addresses while the kernel is executing in its current
 * identity-addressed environment.
 *
 * This implementation intentionally supports only 32-bit DMA addresses.
 * Every DMA object is statically allocated and suitably aligned.
 */
typedef struct __attribute__((packed)) {
    uint8_t  command_fis_length;
    uint8_t  flags;
    uint16_t prdt_length;
    volatile uint32_t transferred_bytes;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
} ahci_command_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  fis_type;
    uint8_t  port_multiplier_and_flags;
    uint8_t  command;
    uint8_t  feature_low;

    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;

    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  feature_high;

    uint8_t  count_low;
    uint8_t  count_high;
    uint8_t  icc;
    uint8_t  control;

    uint8_t  reserved[4];
} ahci_register_fis_t;

typedef struct __attribute__((packed)) {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count_and_interrupt;
} ahci_prdt_entry_t;

typedef struct __attribute__((packed, aligned(128))) {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    ahci_prdt_entry_t prdt[1];
} ahci_command_table_t;

typedef struct __attribute__((aligned(1024))) {
    ahci_command_header_t command_list[32];
} ahci_command_list_t;

typedef struct __attribute__((aligned(256))) {
    uint8_t bytes[256];
} ahci_received_fis_t;

typedef struct __attribute__((aligned(128))) {
    ahci_command_table_t command_table;
} ahci_aligned_command_table_t;

typedef struct __attribute__((aligned(2))) {
    uint8_t bytes[AHCI_SECTOR_SIZE];
} ahci_sector_buffer_t;

static ahci_controller_info_t
    controllers[AHCI_MAX_CONTROLLERS];

static ahci_port_info_t
    ports[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS];

static uint32_t controller_count;

/*
 * A failed port is no longer used for I/O.
 * If its engine cannot be stopped, the shared DMA workspace is unsafe
 * for every port, so all further commands are rejected.
 */
static uint8_t port_flush_ext_supported[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS];
static uint8_t port_faulted[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS];
static int dma_workspace_unsafe;

/* One serialized DMA workspace, shared by all ports. */
static ahci_command_list_t dma_command_list;
static ahci_received_fis_t dma_received_fis;
static ahci_aligned_command_table_t dma_command_table;
static ahci_sector_buffer_t dma_sector_buffer;
static uint16_t identify_words[256];

static uint32_t mmio_read32(uint32_t base, uint32_t offset)
{
    volatile uint32_t *address =
        (volatile uint32_t *)(uintptr_t)(base + offset);

    return *address;
}

static void mmio_write32(uint32_t base,
                         uint32_t offset,
                         uint32_t value)
{
    volatile uint32_t *address =
        (volatile uint32_t *)(uintptr_t)(base + offset);

    *address = value;
}

static uint32_t port_register(uint32_t controller_index,
                              uint32_t port_number,
                              uint32_t offset)
{
    return controllers[controller_index].abar
         + HBA_PORT_BASE
         + port_number * HBA_PORT_SIZE
         + offset;
}

static uint32_t port_read(uint32_t controller_index,
                          uint32_t port_number,
                          uint32_t offset)
{
    return mmio_read32(
        port_register(controller_index, port_number, offset), 0
    );
}

static void port_write(uint32_t controller_index,
                       uint32_t port_number,
                       uint32_t offset,
                       uint32_t value)
{
    mmio_write32(
        port_register(controller_index, port_number, offset),
        0,
        value
    );
}

static int wait_register_clear(uint32_t controller_index,
                               uint32_t port_number,
                               uint32_t offset,
                               uint32_t mask)
{
    uint32_t i;

    for (i = 0; i < AHCI_POLL_LIMIT; ++i) {
        if ((port_read(controller_index, port_number, offset) & mask)
            == 0) {
            return 1;
        }
    }

    return 0;
}

static int wait_port_ready(uint32_t controller_index,
                           uint32_t port_number)
{
    uint32_t i;

    for (i = 0; i < AHCI_POLL_LIMIT; ++i) {
        uint32_t status = port_read(
            controller_index, port_number, PX_TFD
        );

        if (status & (ATA_STATUS_ERR | ATA_STATUS_BSY |
                      ATA_STATUS_DRQ)) {
            continue;
        }

        return 1;
    }

    return 0;
}

static int dma_address_valid(const void *pointer, uint32_t length)
{
    uintptr_t address = (uintptr_t)pointer;

    if (length == 0 || address > 0xFFFFFFFFu) {
        return 0;
    }

    if ((uintptr_t)(length - 1u) > 0xFFFFFFFFu - address) {
        return 0;
    }

    return 1;
}

static void clear_dma_workspace(void)
{
    volatile uint8_t *p;
    size_t i;

    p = (volatile uint8_t *)&dma_command_list;
    for (i = 0; i < sizeof(dma_command_list); ++i) {
        p[i] = 0;
    }

    p = (volatile uint8_t *)&dma_received_fis;
    for (i = 0; i < sizeof(dma_received_fis); ++i) {
        p[i] = 0;
    }

    p = (volatile uint8_t *)&dma_command_table;
    for (i = 0; i < sizeof(dma_command_table); ++i) {
        p[i] = 0;
    }

    p = (volatile uint8_t *)&dma_sector_buffer;
    for (i = 0; i < sizeof(dma_sector_buffer); ++i) {
        p[i] = 0;
    }
}

static void copy_identify_model(char model[AHCI_MODEL_LENGTH],
                                const uint16_t *words)
{
    uint32_t i;

    for (i = 0; i < 20; ++i) {
        model[i * 2] = (char)(words[27 + i] >> 8);
        model[i * 2 + 1] = (char)(words[27 + i] & 0xFF);
    }

    model[40] = '\0';

    for (i = 40; i > 0; --i) {
        if (model[i - 1] == ' ' || model[i - 1] == '\0') {
            model[i - 1] = '\0';
        } else {
            break;
        }
    }
}

static uint64_t identify_sector_count(const uint16_t *words)
{
    uint64_t count;

    /*
     * IDENTIFY word 83 bit 10 advertises 48-bit LBA support.
     */
    if (words[83] & (1u << 10)) {
        count = (uint64_t)words[100]
              | ((uint64_t)words[101] << 16)
              | ((uint64_t)words[102] << 32)
              | ((uint64_t)words[103] << 48);
    } else {
        count = (uint64_t)words[60]
              | ((uint64_t)words[61] << 16);
    }

    return count;
}

/*
 * Stop the command and FIS engines before changing their DMA addresses.
 * The controller must be idle before the command list and received-FIS
 * buffer are installed.
 */
static int stop_port_engine(uint32_t controller_index,
                            uint32_t port_number)
{
    uint32_t command;

    command = port_read(controller_index, port_number, PX_CMD);

    command &= ~PX_CMD_ST;
    port_write(controller_index, port_number, PX_CMD, command);

    if (!wait_register_clear(controller_index, port_number,
                             PX_CMD, PX_CMD_CR)) {
        return 0;
    }

    command = port_read(controller_index, port_number, PX_CMD);
    command &= ~PX_CMD_FRE;
    port_write(controller_index, port_number, PX_CMD, command);

    if (!wait_register_clear(controller_index, port_number,
                             PX_CMD, PX_CMD_FR)) {
        return 0;
    }

    return 1;
}

static int start_port_engine(uint32_t controller_index,
                             uint32_t port_number)
{
    uint32_t command;

    command = port_read(controller_index, port_number, PX_CMD);
    command |= PX_CMD_FRE;
    port_write(controller_index, port_number, PX_CMD, command);

    command = port_read(controller_index, port_number, PX_CMD);
    command |= PX_CMD_ST;
    port_write(controller_index, port_number, PX_CMD, command);

    return 1;
}


static int ahci_prepare_port(uint32_t controller_index,
                             uint32_t port_number)
{
    uint32_t abar;
    uint32_t command;
    uint16_t pci_command;
    uint32_t i;

    if (controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS ||
        dma_workspace_unsafe) {
        return 0;
    }

    if (port_faulted[controller_index][port_number] ||
        !ports[controller_index][port_number].detected ||
        ports[controller_index][port_number].device_type !=
            AHCI_DEVICE_SATA) {
        return 0;
    }

    abar = controllers[controller_index].abar;

    if (!dma_address_valid(&dma_command_list,
                           sizeof(dma_command_list)) ||
        !dma_address_valid(&dma_received_fis,
                           sizeof(dma_received_fis)) ||
        !dma_address_valid(&dma_command_table,
                           sizeof(dma_command_table)) ||
        !dma_address_valid(&dma_sector_buffer,
                           sizeof(dma_sector_buffer))) {
        return 0;
    }

    if (((uintptr_t)&dma_command_list & 1023u) != 0 ||
        ((uintptr_t)&dma_received_fis & 255u) != 0 ||
        ((uintptr_t)&dma_command_table & 127u) != 0) {
        return 0;
    }

    /*
     * Use the BDF saved for this initialized AHCI controller.
     * Do not use controller_index to index the PCI discovery list:
     * unsupported controllers may have been skipped during discovery.
     */
    pci_command = pci_read_config16(
        controllers[controller_index].bus,
        controllers[controller_index].device,
        controllers[controller_index].function,
        PCI_COMMAND_REGISTER
    );

    if (!(pci_command & PCI_COMMAND_MEMORY)) {
        return 0;
    }

    if (!(pci_command & PCI_COMMAND_BUSMASTER)) {
        pci_write_config16(
            controllers[controller_index].bus,
            controllers[controller_index].device,
            controllers[controller_index].function,
            PCI_COMMAND_REGISTER,
            (uint16_t)(pci_command | PCI_COMMAND_BUSMASTER)
        );

        pci_command = pci_read_config16(
            controllers[controller_index].bus,
            controllers[controller_index].device,
            controllers[controller_index].function,
            PCI_COMMAND_REGISTER
        );

        if (!(pci_command & PCI_COMMAND_BUSMASTER)) {
            return 0;
        }
    }

    command = mmio_read32(abar, HBA_GHC);

    if (!(command & HBA_GHC_AE)) {
        mmio_write32(abar, HBA_GHC, command | HBA_GHC_AE);
    }

    /*
     * Never alter the DMA workspace until the port's engines
     * have stopped. A stop failure blocks all future AHCI I/O.
     */
    if (!stop_port_engine(controller_index, port_number)) {
        port_faulted[controller_index][port_number] = 1;
        dma_workspace_unsafe = 1;
        return 0;
    }

    clear_dma_workspace();

    mmio_write32(abar, HBA_IS, 0xFFFFFFFFu);

    port_write(controller_index, port_number, PX_IS, 0xFFFFFFFFu);
    port_write(controller_index, port_number, PX_SERR, 0xFFFFFFFFu);

    port_write(
        controller_index, port_number, PX_CLB,
        (uint32_t)(uintptr_t)&dma_command_list
    );
    port_write(controller_index, port_number, PX_CLBU, 0);

    port_write(
        controller_index, port_number, PX_FB,
        (uint32_t)(uintptr_t)&dma_received_fis
    );
    port_write(controller_index, port_number, PX_FBU, 0);

    for (i = 0; i < 32; ++i) {
        dma_command_list.command_list[i].command_table_base = 0;
        dma_command_list.command_list[i].command_table_base_upper = 0;
    }

    dma_command_list.command_list[0].command_table_base =
        (uint32_t)(uintptr_t)&dma_command_table.command_table;

    dma_command_list.command_list[0].command_table_base_upper = 0;

    return start_port_engine(controller_index, port_number);
}

static int ahci_fail_command(uint32_t controller_index,
                             uint32_t port_number)
{
    port_faulted[controller_index][port_number] = 1;

    /*
     * Do not reuse the global DMA workspace unless both command
     * and FIS engines have stopped successfully.
     */
    if (!stop_port_engine(controller_index, port_number)) {
        dma_workspace_unsafe = 1;
        return 0;
    }

    return 0;
}

static int ahci_issue_command(uint32_t controller_index,
                              uint32_t port_number,
                              uint8_t command,
                              uint64_t lba,
                              uint16_t sector_count,
                              uint8_t *buffer,
                              int identify)
{
    ahci_command_header_t *header;
    ahci_command_table_t *table;
    ahci_register_fis_t *fis;
    ahci_prdt_entry_t *prdt;
    uint32_t i;
    uint32_t status;
    uint32_t interrupt_status;
    int has_data;
    int is_write;
    int is_flush;

    is_flush = (!identify &&
                command == ATA_CMD_FLUSH_CACHE_EXT);

    is_write = (!identify &&
                command == ATA_CMD_WRITE_DMA_EXT);

    has_data = !is_flush;

    if (controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS ||
        dma_workspace_unsafe ||
        port_faulted[controller_index][port_number]) {
        return 0;
    }

    if (identify) {
        if (command != ATA_CMD_IDENTIFY_DEVICE ||
            sector_count != 1 || !buffer) {
            return 0;
        }
    } else if (is_flush) {
        if (sector_count != 0 || buffer != 0 ||
            !port_flush_ext_supported[controller_index][port_number]) {
            return 0;
        }
    } else {
        if ((command != ATA_CMD_READ_DMA_EXT &&
             command != ATA_CMD_WRITE_DMA_EXT) ||
            sector_count != 1 || !buffer ||
            lba > 0x0000FFFFFFFFFFFFull) {
            return 0;
        }
    }

    if (has_data &&
        !dma_address_valid(buffer, AHCI_SECTOR_SIZE)) {
        return 0;
    }

    if (!ahci_prepare_port(controller_index, port_number)) {
        return 0;
    }

    if (!wait_port_ready(controller_index, port_number)) {
        return ahci_fail_command(controller_index, port_number);
    }

    header = &dma_command_list.command_list[0];
    table = &dma_command_table.command_table;
    fis = (ahci_register_fis_t *)table->cfis;
    prdt = &table->prdt[0];

    for (i = 0; i < sizeof(*table); ++i) {
        ((volatile uint8_t *)table)[i] = 0;
    }

    header->command_fis_length = 5;
    header->flags = is_write ? AHCI_CMD_HEADER_WRITE : 0;
    header->prdt_length = has_data ? 1 : 0;
    header->transferred_bytes = 0;
    header->command_table_base = (uint32_t)(uintptr_t)table;
    header->command_table_base_upper = 0;

    fis->fis_type = 0x27;
    fis->port_multiplier_and_flags = 0x80;

    if (identify) {
        fis->command = ATA_CMD_IDENTIFY_DEVICE;
        fis->device = 0;
    } else if (is_flush) {
        /*
         * FLUSH CACHE EXT has no data phase:
         * no PRDT entry, no buffer, and a zero sector count.
         */
        fis->command = ATA_CMD_FLUSH_CACHE_EXT;
        fis->device = 0;
    } else {
        fis->command = command;
        fis->device = 1u << 6;

        fis->lba0 = (uint8_t)lba;
        fis->lba1 = (uint8_t)(lba >> 8);
        fis->lba2 = (uint8_t)(lba >> 16);
        fis->lba3 = (uint8_t)(lba >> 24);
        fis->lba4 = (uint8_t)(lba >> 32);
        fis->lba5 = (uint8_t)(lba >> 40);

        fis->count_low = (uint8_t)sector_count;
        fis->count_high = (uint8_t)(sector_count >> 8);
    }

    if (has_data) {
        prdt->data_base = (uint32_t)(uintptr_t)buffer;
        prdt->data_base_upper = 0;
        prdt->reserved = 0;
        prdt->byte_count_and_interrupt =
            (AHCI_SECTOR_SIZE - 1u) | (1u << 31);
    }

    port_write(controller_index, port_number, PX_IS, 0xFFFFFFFFu);
    port_write(controller_index, port_number, PX_SERR, 0xFFFFFFFFu);

    __asm__ volatile ("" ::: "memory");

    port_write(controller_index, port_number, PX_CI, 1u);

    for (i = 0; i < AHCI_POLL_LIMIT; ++i) {
        status = port_read(controller_index, port_number, PX_TFD);
        interrupt_status =
            port_read(controller_index, port_number, PX_IS);

        if ((status & ATA_STATUS_ERR) ||
            (interrupt_status & PX_IS_TFES)) {
            return ahci_fail_command(
                controller_index, port_number
            );
        }

        if ((port_read(controller_index, port_number, PX_CI) & 1u)
            == 0) {
            __asm__ volatile ("" ::: "memory");

            status = port_read(
                controller_index, port_number, PX_TFD
            );
            interrupt_status = port_read(
                controller_index, port_number, PX_IS
            );

            if ((status & (ATA_STATUS_ERR |
                           ATA_STATUS_BSY |
                           ATA_STATUS_DRQ)) ||
                (interrupt_status & PX_IS_TFES)) {
                return ahci_fail_command(
                    controller_index, port_number
                );
            }

            return 1;
        }
    }

    /* Timed out: stop the engine before permitting DMA-buffer reuse. */
    return ahci_fail_command(controller_index, port_number);
}

int ahci_initialize(void)
{
    uint32_t i;
    uint32_t port_number;
    uint32_t count;
    uint32_t abar;
    uint32_t cap;
    uint32_t pi;
    uint32_t version;
    uint32_t ssts;
    uint32_t signature;
    uint32_t task_file;
    uint32_t command;
    uint16_t pci_command;
    pci_ahci_controller_t pci_controller;

    controller_count = 0;
    dma_workspace_unsafe = 0;

    /*
     * Reset all discovered-controller and port state before
     * scanning the PCI AHCI controller list again.
     */
    for (i = 0; i < AHCI_MAX_CONTROLLERS; ++i) {
        controllers[i].detected = 0;

        for (port_number = 0; port_number < AHCI_MAX_PORTS;
             ++port_number) {
            ports[i][port_number].detected = 0;
            ports[i][port_number].identified = 0;
            ports[i][port_number].device_type = AHCI_DEVICE_NONE;
            ports[i][port_number].port_number =
                (uint8_t)port_number;
            ports[i][port_number].model[0] = '\0';
            ports[i][port_number].sector_count = 0;
            ports[i][port_number].sata_status = 0;
            ports[i][port_number].sata_signature = 0;
            ports[i][port_number].task_file_data = 0;

            port_faulted[i][port_number] = 0;
            port_flush_ext_supported[i][port_number] = 0;
        }
    }

    count = pci_ahci_controller_count();

    if (count > AHCI_MAX_CONTROLLERS) {
        count = AHCI_MAX_CONTROLLERS;
    }

    terminal_writeline("Initializing AHCI discovery...");

    for (i = 0; i < count; ++i) {
        /*
         * This index refers to the PCI discovery list, not the
         * compacted list of controllers accepted by this driver.
         */
        if (!pci_get_ahci_controller(i, &pci_controller)) {
            continue;
        }

        if (pci_controller.bar5 == 0 ||
            pci_controller.bar5 == 0xFFFFFFFFu ||
            (pci_controller.bar5 & 1u) ||
            ((pci_controller.bar5 & 0x06u) == 0x04u)) {
            terminal_writeline(
                "AHCI controller skipped: unsupported BAR5."
            );
            continue;
        }

        abar = pci_controller.bar5 & ~0x0Fu;

        if (abar == 0 ||
            abar > 0xFFFFFFFFu - AHCI_MMIO_REQUIRED_SIZE) {
            terminal_writeline(
                "AHCI controller skipped: invalid MMIO address."
            );
            continue;
        }

        pci_command = pci_read_config16(
            pci_controller.bus,
            pci_controller.device,
            pci_controller.function,
            PCI_COMMAND_REGISTER
        );

        if (!(pci_command & PCI_COMMAND_MEMORY)) {
            terminal_writeline(
                "AHCI controller skipped: memory decoding disabled."
            );
            continue;
        }

        cap = mmio_read32(abar, HBA_CAP);
        pi = mmio_read32(abar, HBA_PI);
        version = mmio_read32(abar, HBA_VS);

        /*
         * This driver deliberately uses 32-bit DMA addresses.
         */
        command = mmio_read32(abar, HBA_GHC);
        mmio_write32(abar, HBA_GHC, command | HBA_GHC_AE);

        /*
         * Store accepted controllers contiguously. Keep their
         * actual PCI bus/device/function values for later I/O.
         */
        controllers[controller_count].detected = 1;
        controllers[controller_count].bus = pci_controller.bus;
        controllers[controller_count].device =
            pci_controller.device;
        controllers[controller_count].function =
            pci_controller.function;
        controllers[controller_count].vendor_id =
            pci_controller.vendor_id;
        controllers[controller_count].device_id =
            pci_controller.device_id;
        controllers[controller_count].abar = abar;
        controllers[controller_count].capabilities = cap;
        controllers[controller_count].implemented_ports = pi;
        controllers[controller_count].version = version;

        terminal_writeline("AHCI controller initialized.");

        for (port_number = 0; port_number < AHCI_MAX_PORTS;
             ++port_number) {
            ahci_port_info_t *port;

            if (!(pi & (1u << port_number))) {
                continue;
            }

            ssts = mmio_read32(
                abar + HBA_PORT_BASE +
                    port_number * HBA_PORT_SIZE + PX_SSTS,
                0
            );

            signature = mmio_read32(
                abar + HBA_PORT_BASE +
                    port_number * HBA_PORT_SIZE + PX_SIG,
                0
            );

            task_file = mmio_read32(
                abar + HBA_PORT_BASE +
                    port_number * HBA_PORT_SIZE + PX_TFD,
                0
            );

            if ((ssts & SATA_DET_MASK) != SATA_DET_READY ||
                (ssts & SATA_IPM_MASK) != SATA_IPM_ACTIVE) {
                continue;
            }

            port = &ports[controller_count][port_number];

            port->detected = 1;
            port->port_number = (uint8_t)port_number;
            port->sata_status = ssts;
            port->sata_signature = (uint8_t)(signature & 0xFFu);
            port->task_file_data = task_file;

            if (signature == SATA_SIG_ATA) {
                port->device_type = AHCI_DEVICE_SATA;
            } else if (signature == SATA_SIG_ATAPI) {
                port->device_type = AHCI_DEVICE_ATAPI;
            } else if (signature == SATA_SIG_SEMB) {
                port->device_type = AHCI_DEVICE_SEMB;
            } else if (signature == SATA_SIG_PM) {
                port->device_type = AHCI_DEVICE_PORT_MULTIPLIER;
            } else {
                port->device_type = AHCI_DEVICE_NONE;
            }
        }

        ++controller_count;
    }

    terminal_write("Usable AHCI controllers: ");
    terminal_putchar((char)('0' + (controller_count % 10u)));
    terminal_putchar('\n');

    return controller_count != 0;
}

uint32_t ahci_controller_count(void)
{
    return controller_count;
}

int ahci_get_controller_info(uint32_t index,
                             ahci_controller_info_t *info)
{
    if (!info || index >= controller_count) {
        return 0;
    }

    *info = controllers[index];
    return 1;
}

int ahci_get_port_info(uint32_t controller_index,
                       uint32_t port_number,
                       ahci_port_info_t *info)
{
    if (!info ||
        controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS) {
        return 0;
    }

    *info = ports[controller_index][port_number];
    return 1;
}

int ahci_identify_device(uint32_t controller_index,
                         uint32_t port_number)
{
    ahci_port_info_t *port;
    uint32_t i;

    if (controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS) {
        return 0;
    }

    port = &ports[controller_index][port_number];

    if (!port->detected || port->device_type != AHCI_DEVICE_SATA) {
        return 0;
    }

    for (i = 0; i < 256; ++i) {
        identify_words[i] = 0;
    }

    if (!ahci_issue_command(
            controller_index,
            port_number,
            ATA_CMD_IDENTIFY_DEVICE,
            0,
            1,
            (uint8_t *)identify_words,
            1)) {
        return 0;
    }

    port->sector_count = identify_sector_count(identify_words);

    if (port->sector_count == 0) {
        return 0;
    }

    copy_identify_model(port->model, identify_words);
    port->identified = 1;

    /*
     * Word 83's command-set bits are valid only when bits 15:14
     * equal 01b. Bit 12 advertises FLUSH CACHE EXT.
     */
    port_flush_ext_supported[controller_index][port_number] =
        ((identify_words[83] & 0xC000u) == 0x4000u) &&
        ((identify_words[83] & (1u << 12)) != 0);

    return 1;
}

int ahci_read_sector(uint32_t controller_index,
                     uint32_t port_number,
                     uint64_t lba,
                     uint8_t *buffer)
{
    ahci_port_info_t *port;

    if (!buffer ||
        controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS) {
        return 0;
    }

    port = &ports[controller_index][port_number];

    if (!port->identified || lba >= port->sector_count) {
        return 0;
    }

    if (!ahci_issue_command(
            controller_index,
            port_number,
            ATA_CMD_READ_DMA_EXT,
            lba,
            1,
            dma_sector_buffer.bytes,
            0)) {
        return 0;
    }

    /*
     * Copy from the controller's DMA buffer only after command
     * completion has been observed.
     */
    {
        uint32_t i;

        for (i = 0; i < AHCI_SECTOR_SIZE; ++i) {
            buffer[i] = dma_sector_buffer.bytes[i];
        }
    }

    return 1;
}

int ahci_flush_cache(uint32_t controller_index,
                     uint32_t port_number)
{
    if (controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS ||
        !ports[controller_index][port_number].identified ||
        !port_flush_ext_supported[controller_index][port_number]) {
        return 0;
    }

    return ahci_issue_command(
        controller_index,
        port_number,
        ATA_CMD_FLUSH_CACHE_EXT,
        0,
        0,
        0,
        0
    );
}

int ahci_write_sector(uint32_t controller_index,
                      uint32_t port_number,
                      uint64_t lba,
                      const uint8_t *buffer)
{
    ahci_port_info_t *port;

    if (!buffer ||
        controller_index >= controller_count ||
        port_number >= AHCI_MAX_PORTS ||
        dma_workspace_unsafe ||
        port_faulted[controller_index][port_number]) {
        return 0;
    }

    port = &ports[controller_index][port_number];

    if (!port->identified ||
        lba >= port->sector_count ||
        !port_flush_ext_supported[controller_index][port_number]) {
        return 0;
    }

    /*
     * DMA reads the source buffer directly for a write.
     * The caller's buffer must remain valid until this synchronous
     * operation returns. The kernel currently uses identity mapping.
     */
    if (!ahci_issue_command(
            controller_index,
            port_number,
            ATA_CMD_WRITE_DMA_EXT,
            lba,
            1,
            (uint8_t *)(uintptr_t)buffer,
            0)) {
        return 0;
    }

    /*
     * Report success only after the drive confirms its cache flush.
     * A flush failure means the sector may have been written, but
     * durable completion has not been confirmed.
     */
    return ahci_flush_cache(controller_index, port_number);
}
