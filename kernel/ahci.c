
#include <stdint.h>

#include "ahci.h"
#include "pci.h"
#include "terminal.h"

#define AHCI_PCI_CLASS       0x01
#define AHCI_PCI_SUBCLASS    0x06
#define AHCI_PCI_PROGIF      0x01

#define AHCI_REG_CAP         0x00
#define AHCI_REG_GHC         0x04
#define AHCI_REG_IS          0x08
#define AHCI_REG_PI          0x0C
#define AHCI_REG_VS          0x10
#define AHCI_REG_BOHC        0x28
#define AHCI_PORT_BASE       0x100
#define AHCI_PORT_STRIDE     0x80

#define AHCI_PORT_SSTS       0x28
#define AHCI_PORT_SIG        0x24
#define AHCI_PORT_TFD        0x20

#define AHCI_SIG_ATA         0x00000101u
#define AHCI_SIG_ATAPI       0xEB140101u
#define AHCI_SIG_SEMB        0xC33C0101u
#define AHCI_SIG_PM          0x96690101u

#define PCI_REG_COMMAND          0x04
#define PCI_COMMAND_MEMORY       0x0002u

#define AHCI_MMIO_REQUIRED_SIZE  0x1100u

static ahci_controller_info_t controllers[AHCI_MAX_CONTROLLERS];
static ahci_port_info_t ports[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS];
static uint32_t controller_count;

static uint32_t mmio_read32(uint32_t base, uint32_t offset)
{
    volatile uint32_t *reg =
        (volatile uint32_t *)(uintptr_t)(base + offset);

    return *reg;
}

static void mmio_write32(uint32_t base,
                         uint32_t offset,
                         uint32_t value)
{
    volatile uint32_t *reg =
        (volatile uint32_t *)(uintptr_t)(base + offset);

    *reg = value;
}

static void ahci_write_hex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    terminal_putchar(digits[(value >> 4) & 0x0F]);
    terminal_putchar(digits[value & 0x0F]);
}

static void ahci_write_hex32(uint32_t value)
{
    ahci_write_hex8((uint8_t)(value >> 24));
    ahci_write_hex8((uint8_t)(value >> 16));
    ahci_write_hex8((uint8_t)(value >> 8));
    ahci_write_hex8((uint8_t)value);
}

static int ahci_bar5_get_base(uint32_t raw_bar, uint32_t *base)
{
    uint32_t bar_type;

    if (base == 0) {
        return 0;
    }

    if (raw_bar == 0 || raw_bar == 0xFFFFFFFFu) {
        return 0;
    }

    /* AHCI BAR5 must describe a memory-mapped register region. */
    if ((raw_bar & 1u) != 0) {
        return 0;
    }

    bar_type = (raw_bar >> 1) & 3u;

    /*
     * This initial 32-bit driver supports only a 32-bit memory BAR.
     * A 64-bit BAR requires reading and validating the upper half.
     */
    if (bar_type != 0) {
        return 0;
    }

    *base = raw_bar & 0xFFFFFFF0u;

    if (*base == 0) {
        return 0;
    }

    return 1;
}

static void ahci_inspect_port(uint32_t controller_index,
                              uint32_t port_number,
                              uint32_t abar)
{
    uint32_t offset;
    uint32_t ssts;
    uint32_t signature;
    uint32_t tfd;

    ahci_port_info_t *info;

    if (controller_index >= AHCI_MAX_CONTROLLERS
        || port_number >= AHCI_MAX_PORTS) {
        return;
    }

    info = &ports[controller_index][port_number];

    offset = AHCI_PORT_BASE + port_number * AHCI_PORT_STRIDE;

    ssts = mmio_read32(abar, offset + AHCI_PORT_SSTS);
    signature = mmio_read32(abar, offset + AHCI_PORT_SIG);
    tfd = mmio_read32(abar, offset + AHCI_PORT_TFD);

    info->detected = 1;
    info->port_number = (uint8_t)port_number;
    info->sata_status = ssts;
    info->sata_signature = signature;
    info->task_file_data = tfd;

    /*
     * SSTS.DET == 3 means a device is present and communication
     * has been established at the physical SATA link level.
     */
    if ((ssts & 0x0Fu) == 3u && ((ssts >> 8) & 0x0Fu) == 1u) {
        if (signature == AHCI_SIG_ATA) {
            info->device_type = 1;
        } else if (signature == AHCI_SIG_ATAPI) {
            info->device_type = 2;
        } else if (signature == AHCI_SIG_SEMB) {
            info->device_type = 3;
        } else if (signature == AHCI_SIG_PM) {
            info->device_type = 4;
        } else {
            info->device_type = 5;
        }
    } else {
        info->device_type = 0;
    }
}

int ahci_initialize(void)
{
    uint32_t found;
    uint32_t i;
    uint32_t port;
    uint32_t abar;
    uint32_t raw_bar;
    uint32_t cap;
    uint32_t pi;
    uint32_t version;
    pci_ahci_controller_t pci_info;

    controller_count = 0;

    for (i = 0; i < AHCI_MAX_CONTROLLERS; ++i) {
        controllers[i].detected = 0;

        for (port = 0; port < AHCI_MAX_PORTS; ++port) {
            ports[i][port].detected = 0;
        }
    }

    found = pci_ahci_controller_count();

    uint16_t pci_command;

    pci_command = pci_read_config16(
        pci_info.bus,
        pci_info.device,
        pci_info.function,
        PCI_REG_COMMAND
    );

    if ((pci_command & PCI_COMMAND_MEMORY) == 0) {
        terminal_writeline(
            "AHCI: PCI memory-space decoding is disabled; skipping."
        );
        continue;
    }

    terminal_writeline("AHCI: inspecting SATA controllers...");

    for (i = 0; i < found &&
            controller_count < AHCI_MAX_CONTROLLERS; ++i) {
        ahci_controller_info_t *info;
        uint16_t pci_command;

        if (!pci_get_ahci_controller(i, &pci_info)) {
            continue;
        }

        pci_command = pci_read_config16(
            pci_info.bus,
            pci_info.device,
            pci_info.function,
            PCI_REG_COMMAND
        );

        if ((pci_command & PCI_COMMAND_MEMORY) == 0) {
            terminal_writeline(
                "AHCI: PCI memory-space decoding is disabled; skipping."
            );
            continue;
        }

        raw_bar = pci_info.bar5;

        if (!ahci_bar5_get_base(raw_bar, &abar)) {
            terminal_writeline(
                "AHCI: unsupported or invalid BAR5; skipping controller."
            );
            continue;
        }

        if (abar > 0xFFFFFFFFu - AHCI_MMIO_REQUIRED_SIZE) {
            terminal_writeline(
                "AHCI: MMIO register range overflows; skipping controller."
            );
            continue;
        }

        /*
         * This driver currently assumes physical MMIO addresses can be
         * accessed directly. The current boot path does not enable paging.
         * Do not dereference BAR5 unless that assumption remains true.
         */
        cap = mmio_read32(abar, AHCI_REG_CAP);
        pi = mmio_read32(abar, AHCI_REG_PI);
        version = mmio_read32(abar, AHCI_REG_VS);

        info = &controllers[controller_count];

        info->detected = 1;
        info->bus = pci_info.bus;
        info->device = pci_info.device;
        info->function = pci_info.function;
        info->vendor_id = pci_info.vendor_id;
        info->device_id = pci_info.device_id;
        info->abar = abar;
        info->capabilities = cap;
        info->implemented_ports = pi;
        info->version = version;

        terminal_writeline("AHCI controller registers read.");
        terminal_write("  ABAR: 0x");
        ahci_write_hex32(abar);
        terminal_putchar('\n');

        terminal_write("  Version: 0x");
        ahci_write_hex32(version);
        terminal_putchar('\n');

        terminal_write("  CAP: 0x");
        ahci_write_hex32(cap);
        terminal_putchar('\n');

        terminal_write("  PI: 0x");
        ahci_write_hex32(pi);
        terminal_putchar('\n');

        for (port = 0; port < AHCI_MAX_PORTS; ++port) {
            if ((pi & (1u << port)) == 0) {
                continue;
            }

            ahci_inspect_port(controller_count, port, abar);

            terminal_write("  Port ");
            terminal_putchar((char)('0' + (port / 10u)));
            terminal_putchar((char)('0' + (port % 10u)));
            terminal_write(": SSTS=0x");
            ahci_write_hex32(ports[controller_count][port].sata_status);
            terminal_write(" SIG=0x");
            ahci_write_hex32(ports[controller_count][port].sata_signature);
            terminal_putchar('\n');
        }

        ++controller_count;
    }

    if (controller_count == 0) {
        terminal_writeline(
            "AHCI: no usable controller could be inspected."
        );
        return 0;
    }

    terminal_write("AHCI controllers inspected: ");
    terminal_putchar((char)('0' + (controller_count % 10u)));
    terminal_putchar('\n');

    return 1;
}

uint32_t ahci_controller_count(void)
{
    return controller_count;
}

int ahci_get_controller_info(uint32_t index,
                             ahci_controller_info_t *info)
{
    if (info == 0 || index >= controller_count) {
        return 0;
    }

    *info = controllers[index];
    return 1;
}

int ahci_get_port_info(uint32_t controller_index,
                       uint32_t port_number,
                       ahci_port_info_t *info)
{
    if (info == 0
        || controller_index >= controller_count
        || port_number >= AHCI_MAX_PORTS
        || !ports[controller_index][port_number].detected) {
        return 0;
    }

    *info = ports[controller_index][port_number];
    return 1;
}
