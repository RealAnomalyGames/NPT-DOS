
#include <stdint.h>

#include "pci.h"
#include "terminal.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_MAX_BUSES      256
#define PCI_MAX_DEVICES    32
#define PCI_MAX_FUNCTIONS  8

#define PCI_REG_VENDOR_DEVICE 0x00
#define PCI_REG_CLASS         0x08
#define PCI_REG_HEADER_TYPE   0x0C
#define PCI_REG_BAR5          0x24

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_SATA      0x06
#define PCI_PROGIF_AHCI        0x01

static uint32_t pci_ahci_count;

static pci_ahci_controller_t
    pci_ahci_controllers[PCI_MAX_AHCI_CONTROLLERS];

static inline void pci_outl(uint16_t port, uint32_t value)
{
    __asm__ volatile (
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline void pci_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint32_t pci_inl(uint16_t port)
{
    uint32_t value;

    __asm__ volatile (
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

uint32_t pci_read_config32(uint8_t bus,
                           uint8_t device,
                           uint8_t function,
                           uint8_t offset)
{
    uint32_t address = 0x80000000u
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)device << 11)
                     | ((uint32_t)function << 8)
                     | ((uint32_t)offset & 0xFCu);

    pci_outl(PCI_CONFIG_ADDRESS, address);
    return pci_inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config16(uint8_t bus,
                           uint8_t device,
                           uint8_t function,
                           uint8_t offset)
{
    uint32_t value = pci_read_config32(
        bus, device, function, offset
    );

    return (uint16_t)(
        value >> ((offset & 2u) * 8u)
    );
}

void pci_write_config16(uint8_t bus,
                        uint8_t device,
                        uint8_t function,
                        uint8_t offset,
                        uint16_t value)
{
    uint32_t address = 0x80000000u
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)device << 11)
                     | ((uint32_t)function << 8)
                     | ((uint32_t)offset & 0xFCu);

    pci_outl(PCI_CONFIG_ADDRESS, address);

    pci_outw(
        (uint16_t)(PCI_CONFIG_DATA + (offset & 2u)),
        value
    );
}

static void pci_write_hex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    terminal_putchar(digits[(value >> 4) & 0x0F]);
    terminal_putchar(digits[value & 0x0F]);
}

static void pci_write_hex16(uint16_t value)
{
    pci_write_hex8((uint8_t)(value >> 8));
    pci_write_hex8((uint8_t)value);
}

static void pci_write_hex32(uint32_t value)
{
    pci_write_hex16((uint16_t)(value >> 16));
    pci_write_hex16((uint16_t)value);
}

static void pci_scan_function(uint8_t bus,
                              uint8_t device,
                              uint8_t function,
                              uint32_t vendor_device)
{
    uint32_t class_register;
    uint32_t bar5_register;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;

    vendor_id = (uint16_t)(vendor_device & 0xFFFFu);
    device_id = (uint16_t)(vendor_device >> 16);

    if (vendor_id == 0xFFFFu) {
        return;
    }

    class_register = pci_read_config32(
        bus, device, function, PCI_REG_CLASS
    );

    class_code = (uint8_t)(class_register >> 24);
    subclass = (uint8_t)(class_register >> 16);
    programming_interface = (uint8_t)(class_register >> 8);

    if (class_code != PCI_CLASS_MASS_STORAGE
        || subclass != PCI_SUBCLASS_SATA
        || programming_interface != PCI_PROGIF_AHCI) {
        return;
    }

    ++pci_ahci_count;

    /*
     * Read BAR5 only. Do not write BARs, enable bus mastering,
     * or access the controller's memory-mapped register space.
     */
    bar5_register = pci_read_config32(
        bus, device, function, PCI_REG_BAR5
    );

    
    if (pci_ahci_count <= PCI_MAX_AHCI_CONTROLLERS) {
        uint32_t index = pci_ahci_count - 1u;

        if (index < PCI_MAX_AHCI_CONTROLLERS) {
            pci_ahci_controllers[index].bus = bus;
            pci_ahci_controllers[index].device = device;
            pci_ahci_controllers[index].function = function;
            pci_ahci_controllers[index].vendor_id = vendor_id;
            pci_ahci_controllers[index].device_id = device_id;
            pci_ahci_controllers[index].bar5 = bar5_register;
        }
    }


    terminal_writeline("PCI AHCI controller detected");

    terminal_write("  PCI location: ");
    pci_write_hex8(bus);
    terminal_putchar(':');
    pci_write_hex8(device);
    terminal_putchar('.');
    pci_write_hex8(function);
    terminal_putchar('\n');

    terminal_write("  Vendor ID:    0x");
    pci_write_hex16(vendor_id);
    terminal_putchar('\n');

    terminal_write("  Device ID:    0x");
    pci_write_hex16(device_id);
    terminal_putchar('\n');

    terminal_write("  BAR5 raw:     0x");
    pci_write_hex32(bar5_register);
    terminal_putchar('\n');

    if (bar5_register == 0 || bar5_register == 0xFFFFFFFFu) {
        terminal_writeline("  BAR5 appears unassigned or invalid.");
    } else if (bar5_register & 1u) {
        terminal_writeline("  BAR5 indicates an I/O BAR.");
    } else {
        terminal_writeline(
            "  BAR5 indicates a memory BAR; accessibility not tested."
        );
    }

    terminal_putchar('\n');
}

void pci_initialize(void)
{
    uint32_t vendor_device;
    uint32_t header_register;

    uint16_t bus_index;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t header_type;

    pci_ahci_count = 0;

    terminal_writeline("Scanning PCI configuration space...");

    for (bus_index = 0; bus_index < 256; ++bus_index) {
        bus = (uint8_t)bus_index;

        for (device = 0; device < 32; ++device) {
            vendor_device = pci_read_config32(
                bus, device, 0, PCI_REG_VENDOR_DEVICE
            );

            if ((vendor_device & 0xFFFFu) == 0xFFFFu) {
                continue;
            }

            header_register = pci_read_config32(
                bus, device, 0, PCI_REG_HEADER_TYPE
            );

            header_type = (uint8_t)(header_register >> 16);

            pci_scan_function(bus, device, 0, vendor_device);

            if ((header_type & 0x80u) == 0) {
                continue;
            }

            for (function = 1; function < 8; ++function) {
                vendor_device = pci_read_config32(
                    bus, device, function,
                    PCI_REG_VENDOR_DEVICE
                );

                if ((vendor_device & 0xFFFFu) == 0xFFFFu) {
                    continue;
                }

                pci_scan_function(
                    bus, device, function, vendor_device
                );
            }
        }
    }

    if (pci_ahci_count == 0) {
        terminal_writeline("No PCI AHCI controller detected.");
    } else {
        terminal_write("AHCI controllers detected: ");
        terminal_putchar(
            (pci_ahci_count < 10)
                ? (char)('0' + pci_ahci_count)
                : '*'
        );
        terminal_putchar('\n');
    }

    terminal_writeline("PCI scan complete.");
}

uint32_t pci_ahci_controller_count(void)
{
    if (pci_ahci_count > PCI_MAX_AHCI_CONTROLLERS) {
        return PCI_MAX_AHCI_CONTROLLERS;
    }

    return pci_ahci_count;
}

int pci_get_ahci_controller(
    uint32_t index,
    pci_ahci_controller_t *controller
)
{
    if (controller == 0) {
        return 0;
    }

    if (index >= pci_ahci_controller_count()) {
        return 0;
    }

    *controller = pci_ahci_controllers[index];
    return 1;
}
