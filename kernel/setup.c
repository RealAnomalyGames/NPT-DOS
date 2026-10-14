
#include "setup.h"
#include "terminal.h"
#include "keyboard.h"
#include "ata.h"
#include "pci.h"
#include "ahci.h"

#include <stdint.h>

#define ATA_SECTOR_SIZE       512U
#define NPTFS_SUPERBLOCK_LBA  1U

static const char *setup_device_name(uint32_t device)
{
    switch (device) {
        case 0: return "Primary Master (IDE)";
        case 1: return "Primary Slave (IDE)";
        case 2: return "Secondary Master (IDE)";
        case 3: return "Secondary Slave (IDE)";
        default:
            if (device >= STORAGE_SATA_ID_BASE) {
                return "SATA/AHCI disk";
            }
            return "Unknown";
    }
}

static void setup_print_u32(uint32_t value)
{
    char buffer[11];
    int position = 10;

    buffer[position] = '\0';

    do {
        buffer[--position] =
            (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && position > 0);

    terminal_write(&buffer[position]);
}

static void setup_inspect_sata_ports(void)
{
    uint32_t controller_index;
    uint32_t controllers_found = ahci_controller_count();
    uint32_t devices_found = 0;

    terminal_writeline("");
    terminal_writeline("NPT-DOS SATA / AHCI INSPECTION");
    terminal_writeline("------------------------------");
    terminal_writeline("Mode: READ-ONLY");
    terminal_writeline("");

    if (controllers_found == 0) {
        terminal_writeline("No usable AHCI controller detected.");
        terminal_writeline("");
        terminal_writeline(
            "This does not prove that no SATA disk is installed."
        );
        terminal_writeline(
            "The controller may use IDE mode or an unsupported configuration."
        );
        return;
    }

    for (controller_index = 0;
         controller_index < controllers_found;
         controller_index++) {

        ahci_controller_info_t controller;
        uint32_t port_number;

        if (!ahci_get_controller_info(
                controller_index, &controller)) {
            continue;
        }

        terminal_writeline("AHCI Controller");

        terminal_write("  PCI location: ");
        setup_print_u32(controller.bus);
        terminal_write(":");
        setup_print_u32(controller.device);
        terminal_write(".");
        setup_print_u32(controller.function);
        terminal_writeline("");

        terminal_write("  Vendor ID: ");
        setup_print_u32(controller.vendor_id);
        terminal_writeline("");

        terminal_write("  Device ID: ");
        setup_print_u32(controller.device_id);
        terminal_writeline("");

        terminal_write("  AHCI version register: ");
        setup_print_u32(controller.version);
        terminal_writeline("");

        for (port_number = 0;
             port_number < AHCI_MAX_PORTS;
             port_number++) {

            ahci_port_info_t port;

            if (!ahci_get_port_info(
                    controller_index, port_number, &port)) {
                continue;
            }

            if (!port.detected || port.device_type == 0) {
                continue;
            }

            devices_found++;

            terminal_write("  Port ");
            setup_print_u32(port.port_number);
            terminal_write(": ");

            switch (port.device_type) {
                case 1:
                    terminal_writeline(
                        "SATA ATA device (signature detected)"
                    );
                    break;

                case 2:
                    terminal_writeline(
                        "SATA ATAPI device (signature detected)"
                    );
                    break;

                case 3:
                    terminal_writeline(
                        "Enclosure management bridge"
                    );
                    break;

                case 4:
                    terminal_writeline(
                        "Port multiplier"
                    );
                    break;

                default:
                    terminal_writeline(
                        "Other AHCI port device"
                    );
                    break;
            }

            terminal_write("    SATA status: ");
            setup_print_u32(port.sata_status);
            terminal_writeline("");

            terminal_write("    Signature: ");
            setup_print_u32(port.sata_signature);
            terminal_writeline("");

            terminal_write("    Task-file status: ");
            setup_print_u32(port.task_file_data);
            terminal_writeline("");
        }

        terminal_writeline("");
    }

    terminal_write("Detected device/port entries: ");
    setup_print_u32(devices_found);
    terminal_writeline("");

    terminal_writeline("");
    terminal_writeline(
        "No disk sectors were read or written."
    );
    terminal_writeline(
        "Model and capacity identification are not implemented yet."
    );
}

static void setup_wait_for_key(void)
{
    (void)keyboard_getchar();
}

static void setup_draw_screen(void)
{
    terminal_clear();

    terminal_writeline("================================================");
    terminal_writeline("                  NPT-DOS SETUP                 ");
    terminal_writeline("               Neptune Corporation              ");
    terminal_writeline("================================================");
    terminal_writeline("Welcome to NPT-DOS Setup.");
    terminal_writeline("Choose how you want to use NPT-DOS:");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("| 1. INSTALL TO HARD DISK                      |");
    terminal_writeline("|    Inspect and select a supported IDE disk.  |");
    terminal_writeline("|    Installation requires explicit approval.  |");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("| 2. PORTABLE MODE                             |");
    terminal_writeline("|    Continue booting from the current media.  |");
    terminal_writeline("|    No installer disk writes are performed.   |");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("| 3. INSPECT SATA / AHCI DEVICES               |");
    terminal_writeline("|    Read-only controller and port inspection. |");
    terminal_writeline("|    Does not install or write to disks.       |");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("Press 1, 2, or 3 to select an option.");
    terminal_writeline("Press Q to exit setup.");
}

static int setup_has_nptfs_signature(const uint8_t *sector)
{
    static const char signature[8] = {
        'N', 'P', 'T', 'F', 'S', '0', '0', '1'
    };

    unsigned int i;

    for (i = 0; i < 8U; i++) {
        if (sector[i] != (uint8_t)signature[i])
            return 0;
    }

    return 1;
}

static int setup_sector_is_zero(const uint8_t *sector)
{
    unsigned int i;

    for (i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (sector[i] != 0U)
            return 0;
    }

    return 1;
}

/*
 * Read-only check for the selected physical ATA device.
 *
 * Planned writes:
 *   LBA 0
 *   LBAs 32-47
 *   LBAs 64-319
 *
 * Returns:
 *   1  All planned overwrite sectors were readable and zero.
 *   0  At least one planned overwrite sector contained data.
 *  -1  Capacity or sector read was unavailable.
 */
static int setup_check_planned_write_sectors(
    uint32_t device,
    uint64_t sectors)
{
    uint8_t sector[ATA_SECTOR_SIZE];
    uint32_t lba;

    if (sectors < 320U)
        return -1;

    if (!storage_read_sector(device, 0U, sector))
        return -1;

    if (!setup_sector_is_zero(sector))
        return 0;

    for (lba = 32U; lba <= 47U; lba++) {
        if (!storage_read_sector(device, lba, sector))
            return -1;

        if (!setup_sector_is_zero(sector))
            return 0;
    }

    for (lba = 64U; lba <= 319U; lba++) {
        if (!storage_read_sector(device, lba, sector))
            return -1;

        if (!setup_sector_is_zero(sector))
            return 0;
    }

    return 1;
}

#ifdef NPT_INSTALLER_BUILD

extern const uint8_t installer_boot_start[];
extern const uint8_t installer_stage2_start[];
extern const uint8_t installer_kernel_start[];

#define INSTALLER_BOOT_LBA       0U
#define INSTALLER_STAGE2_LBA     32U
#define INSTALLER_STAGE2_SECTORS 16U
#define INSTALLER_KERNEL_LBA     64U
#define INSTALLER_KERNEL_SECTORS 256U

static int installer_sector_matches(
    const uint8_t *expected,
    const uint8_t *actual)
{
    unsigned int i;

    for (i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (expected[i] != actual[i])
            return 0;
    }

    return 1;
}

static int installer_write_verified_sector(
    uint32_t device,
    uint32_t lba,
    const uint8_t *data)
{
    uint8_t verify[ATA_SECTOR_SIZE];

    if (!storage_write_sector(device, lba, data)) {
        terminal_write("WRITE FAILED at LBA ");
        setup_print_u32(lba);
        terminal_writeline("");
        return 0;
    }

    if (!storage_read_sector(device, lba, verify)) {
        terminal_write("READ-BACK FAILED at LBA ");
        setup_print_u32(lba);
        terminal_writeline("");
        return 0;
    }

    if (!installer_sector_matches(data, verify)) {
        terminal_write("VERIFY FAILED at LBA ");
        setup_print_u32(lba);
        terminal_writeline("");
        return 0;
    }

    return 1;
}

static void installer_install_target(
    uint32_t device,
    const storage_device_info_t *info)
{
    uint8_t sector[ATA_SECTOR_SIZE];
    uint32_t lba;
    uint32_t i;

    terminal_clear();
    terminal_writeline("NPT-DOS INSTALLER");
    terminal_writeline("-----------------");
    terminal_writeline("");
    terminal_write("Target: ");
    terminal_writeline(setup_device_name(device));
    terminal_write("Model: ");
    terminal_writeline(info ? info->model : "(unknown)");
    terminal_writeline("");
    terminal_writeline("Performing final read-only checks...");

    if (info == 0 || info->sector_count < 320U) {
        terminal_writeline("SAFETY STOP: Target capacity is insufficient.");
        goto failed;
    }

    /* Recheck the selected target immediately before writing. */
    if (!storage_read_sector(device, 1U, sector)) {
        terminal_writeline("SAFETY STOP: Cannot read LBA 1.");
        goto failed;
    }

    if (setup_has_nptfs_signature(sector)) {
        terminal_writeline("SAFETY STOP: NPTFS signature detected.");
        terminal_writeline("Existing NPTFS data will not be overwritten.");
        goto failed;
    }

    /*
     * Require sectors 1-18 to be zero. This is deliberately
     * conservative, but does not prove the whole disk is empty.
     */
    for (lba = 1U; lba <= 18U; lba++) {
        if (!storage_read_sector(device, lba, sector)) {
            terminal_writeline("SAFETY STOP: Cannot inspect LBA.");
            terminal_write("LBA: ");
            setup_print_u32(lba);
            terminal_writeline("");
            goto failed;
        }

        if (!setup_sector_is_zero(sector)) {
            terminal_writeline("SAFETY STOP: Data detected in LBAs 1-18.");
            terminal_writeline("Choose a new, empty hard disk.");
            goto failed;
        }
    }

    if (setup_check_planned_write_sectors(
            device, info->sector_count) != 1) {
        terminal_writeline("SAFETY STOP: Reserved-sector check failed.");
        terminal_writeline("Target was not approved for installation.");
        goto failed;
    }

    terminal_writeline("Preflight checks passed.");
    terminal_writeline("");
    terminal_writeline("Writing second-stage loader...");

    for (i = 0; i < INSTALLER_STAGE2_SECTORS; i++) {
        if (!installer_write_verified_sector(
                device,
                INSTALLER_STAGE2_LBA + i,
                installer_stage2_start + i * ATA_SECTOR_SIZE)) {
            goto failed;
        }
    }

    terminal_writeline("Second-stage loader verified.");
    terminal_writeline("Writing installed kernel...");

    for (i = 0; i < INSTALLER_KERNEL_SECTORS; i++) {
        if (!installer_write_verified_sector(
                device,
                INSTALLER_KERNEL_LBA + i,
                installer_kernel_start + i * ATA_SECTOR_SIZE)) {
            goto failed;
        }
    }

    terminal_writeline("Installed kernel verified.");
    terminal_writeline("Writing boot sector last...");

    if (!installer_write_verified_sector(
            device, INSTALLER_BOOT_LBA, installer_boot_start)) {
        goto failed;
    }

    terminal_writeline("");
    terminal_writeline("INSTALLATION COMPLETED.");
    terminal_writeline("All written sectors passed read-back verification.");
    terminal_writeline("");
    terminal_writeline("Power off, detach the installer ISO,");
    terminal_writeline("and boot from the installed hard disk.");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }

failed:
    terminal_writeline("");
    terminal_writeline("INSTALLATION STOPPED.");
    terminal_writeline("The disk may contain a partial installation.");
    terminal_writeline("Do not assume it is bootable.");
    terminal_writeline("No automatic rollback was attempted.");
    terminal_writeline("");
    terminal_writeline("Press any key to return to setup.");
    setup_wait_for_key();
}

#endif

static void setup_inspect_selected_disk(
    uint32_t device,
    const storage_device_info_t *info)
{
    uint8_t sector[ATA_SECTOR_SIZE];
    uint64_t sectors;
    uint32_t mib_whole;
    uint32_t mib_fraction;
    int read_ok;
    int nptfs_found = 0;
    int overwrite_check;

    terminal_clear();
    terminal_writeline("NPT-DOS SETUP: TARGET INSPECTION");
    terminal_writeline("--------------------------------");
    terminal_writeline("");

    terminal_write("Selected target: ");
    terminal_writeline(setup_device_name(device));

    terminal_write("Model: ");
    terminal_writeline(
        info->model[0] != '\0' ? info->model : "(not reported)");

    terminal_writeline("");

    sectors = info->sector_count;

    if (sectors == 0U) {
        terminal_writeline("Capacity: Unknown");
        terminal_writeline("SAFETY STOP: Capacity unavailable.");
        terminal_writeline("No disk writes were performed.");
        terminal_writeline("Press any key to return.");
        setup_wait_for_key();
        return;
    }

    terminal_write("Addressable sectors: ");
    setup_print_u32((uint32_t)sectors);
    terminal_writeline("");

    mib_whole = sectors / 2048U;
    mib_fraction = ((sectors % 2048U) * 10U) / 2048U;

    terminal_write("Addressable capacity: ");
    setup_print_u32(mib_whole);
    terminal_write(".");
    setup_print_u32(mib_fraction);
    terminal_writeline(" MiB");

    terminal_writeline("Sector size: 512 bytes");
    terminal_writeline("Addressing: ATA LBA28");
    terminal_writeline("");

    read_ok = storage_read_sector(
    device, NPTFS_SUPERBLOCK_LBA, sector);

    if (!read_ok) {
        terminal_writeline("NPTFS status: Cannot read sector 1.");
        terminal_writeline("SAFETY STOP: Read failure.");
    } else if (setup_has_nptfs_signature(sector)) {
        nptfs_found = 1;
        terminal_writeline("NPTFS status: Signature detected.");
        terminal_writeline("Existing NPTFS data may be present.");
        terminal_writeline("The filesystem will not be formatted.");
    } else {
        terminal_writeline("NPTFS status: Signature not detected.");
        terminal_writeline("This does not prove the disk is empty.");
    }

    terminal_writeline("");
    terminal_writeline("Checking planned overwrite sectors...");

    overwrite_check =
        setup_check_planned_write_sectors(device, sectors);

    if (overwrite_check == 1) {
        terminal_writeline("Reserved sectors: All zero.");
        terminal_writeline("This is not proof the disk is empty.");
    } else if (overwrite_check == 0) {
        terminal_writeline("Reserved sectors: DATA DETECTED.");
        terminal_writeline("SAFETY STOP: Do not install here.");
    } else {
        terminal_writeline("Reserved sectors: CHECK FAILED.");
        terminal_writeline("SAFETY STOP: Cannot approve target.");
    }

#ifdef NPT_INSTALLER_BUILD
    if (read_ok &&
        !nptfs_found &&
        overwrite_check == 1 &&
        info->sector_count >= 320U) {

        char key;

        terminal_writeline("");
        terminal_writeline("WARNING: Installation writes disk sectors.");
        terminal_writeline("A clean scan does not prove the entire disk is empty.");
        terminal_writeline("Use only a new, disposable test disk.");
        terminal_writeline("");
        terminal_writeline("Press I to continue to confirmation.");
        terminal_writeline("Press any other key to cancel.");

        key = keyboard_getchar();

        if (key == 'i' || key == 'I') {
            terminal_clear();
            terminal_writeline("FINAL INSTALLATION CONFIRMATION");
            terminal_writeline("--------------------------------");
            terminal_writeline("");

            terminal_write("Target: ");
            terminal_writeline(setup_device_name(device));

            terminal_writeline("Will overwrite LBAs 0, 32-47, and 64-319.");
            terminal_writeline("Will not write LBAs 1-18.");
            terminal_writeline("");
            terminal_writeline("Type Y to authorize installation.");
            terminal_writeline("Any other key cancels.");

            key = keyboard_getchar();

            if (key == 'y' || key == 'Y') {
                installer_install_target(device, info);
            } else {
                terminal_writeline("");
                terminal_writeline("Installation cancelled.");
                setup_wait_for_key();
            }
        }
    } else {
        terminal_writeline("");
        terminal_writeline("INSTALLATION BLOCKED.");
        terminal_writeline("The target did not pass all safety checks.");
        terminal_writeline("No disk writes were performed.");
        terminal_writeline("");
        terminal_writeline("Press any key to return.");
        setup_wait_for_key();
    }
#else
    terminal_writeline("");
    terminal_writeline("INSTALLATION: DISABLED IN BASE KERNEL");
    terminal_writeline("This build performs inspection only.");
    terminal_writeline("No disk writes were performed.");
    terminal_writeline("");
    terminal_writeline("Press any key to return.");
    setup_wait_for_key();
#endif
}

static void setup_select_disk(void)
{
    for (;;) {
        storage_device_info_t devices[STORAGE_IDE_DEVICE_COUNT +
            128U];
        uint32_t total;
        uint32_t found = 0;
        uint32_t i;
        char key;
        int selected = -1;

        terminal_clear();
        terminal_writeline("NPT-DOS SETUP: SELECT TARGET");
        terminal_writeline("----------------------------");
        terminal_writeline("");
        terminal_writeline("Detecting supported IDE and SATA disks...");
        terminal_writeline("");

        if (!storage_initialize()) {
            terminal_writeline("No supported storage disks were detected.");
            terminal_writeline("");
            terminal_writeline("No disk writes were performed.");
            terminal_writeline("Press any key to return to setup.");
            setup_wait_for_key();
            return;
        }

        total = storage_device_count();

        for (i = 0; i < total &&
                    found < (uint32_t)(sizeof(devices) /
                                       sizeof(devices[0])); ++i) {
            storage_device_info_t info;

            if (!storage_get_device_info_at(i, &info)) {
                continue;
            }

            devices[found] = info;

            terminal_write("  ");
            setup_print_u32(found + 1U);
            terminal_write(". ");
            terminal_write(setup_device_name(info.id));
            terminal_writeline("");

            terminal_write("     Model: ");
            terminal_writeline(
                info.model[0] != '\0'
                    ? info.model
                    : "(not reported)");

            terminal_write("     Addressable sectors: ");
            setup_print_u32((uint32_t)info.sector_count);
            terminal_writeline("");

            terminal_write("     Storage ID: ");
            setup_print_u32(info.id);
            terminal_writeline("");
            terminal_writeline("");

            ++found;
        }

        if (found == 0U) {
            terminal_writeline("No usable storage disks were detected.");
            terminal_writeline("Press any key to return to setup.");
            setup_wait_for_key();
            return;
        }

        terminal_writeline("Select a disk by its displayed number.");
        terminal_writeline("Press R to rescan devices.");
        terminal_writeline("Press Q to return to the main setup menu.");

        key = keyboard_getchar();

        if (key == 'q' || key == 'Q') {
            return;
        }

        if (key == 'r' || key == 'R') {
            continue;
        }

        if (key < '1' || key > '9') {
            continue;
        }

        selected = (int)(key - '1');

        if ((uint32_t)selected >= found) {
            continue;
        }

        /*
         * Refresh the selected device by its stable storage ID.
         * Do not treat the displayed list position as the device ID.
         */
        {
            storage_device_info_t current;
            uint32_t device_id = devices[selected].id;

            if (!storage_get_device_info_at(
                    (uint32_t)selected, &current) ||
                current.id != device_id) {
                terminal_writeline("");
                terminal_writeline(
                    "Device list changed; rescan before continuing.");
                setup_wait_for_key();
                continue;
            }

            setup_inspect_selected_disk(
                current.id, &current);
        }
    }
}

int setup_run(void)
{
    for (;;) {
        char key;

        setup_draw_screen();
        key = keyboard_getchar();

        if (key == '1') {
            setup_select_disk();
        } else if (key == '2') {
            terminal_clear();
            terminal_writeline("NPT-DOS Setup");
            terminal_writeline("-------------");
            terminal_writeline("");
            terminal_writeline("Portable Mode selected.");
            terminal_writeline("Continuing NPT-DOS startup...");
            terminal_writeline("");
            return 1;
        } else if (key == '3') {
            terminal_clear();
            setup_inspect_sata_ports();

            terminal_writeline("");
            terminal_writeline("Press any key to return to setup.");
            setup_wait_for_key();
        } else if (key == 'q' || key == 'Q') {
            terminal_clear();
            terminal_writeline("NPT-DOS Setup exited.");
            terminal_writeline("The system will halt.");

            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }
    }
}
