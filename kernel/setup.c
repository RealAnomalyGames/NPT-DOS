#include "setup.h"
#include "terminal.h"
#include "keyboard.h"
#include "ata.h"

#include <stdint.h>

#define ATA_SECTOR_SIZE 512U
#define NPTFS_SUPERBLOCK_LBA 1U

static void setup_print_u32(uint32_t value)
{
    char buffer[11];
    int position = 10;

    buffer[position] = '\0';

    do {
        buffer[--position] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && position > 0);

    terminal_write(&buffer[position]);
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
    terminal_writeline("");
    terminal_writeline("Welcome to NPT-DOS Setup.");
    terminal_writeline("");
    terminal_writeline("Choose how you want to use NPT-DOS:");
    terminal_writeline("");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("| 1. INSTALL TO HARD DISK                      |");
    terminal_writeline("|                                              |");
    terminal_writeline("|    Inspect and select a supported disk.      |");
    terminal_writeline("|    Installation is not enabled in this build.|");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("| 2. PORTABLE MODE                             |");
    terminal_writeline("|                                              |");
    terminal_writeline("|    Continue booting from the current media.  |");
    terminal_writeline("|    No installer disk writes are performed.   |");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("");
    terminal_writeline("Press 1 or 2 to select an option.");
    terminal_writeline("Press Q to exit setup.");
}

static int setup_has_nptfs_signature(const uint8_t *sector)
{
    static const char signature[8] = {
        'N', 'P', 'T', 'F', 'S', '0', '0', '1'
    };

    unsigned int i;

    for (i = 0; i < 8U; i++) {
        if (sector[i] != (uint8_t)signature[i]) {
            return 0;
        }
    }

    return 1;
}

/*
 * Read-only installer safety inspection.
 *
 * The proposed installer would overwrite:
 *   LBA 0
 *   LBA 32-47
 *   LBA 64-319
 *
 * Return values:
 *   1  All planned overwrite sectors were readable and zero.
 *   0  At least one planned overwrite sector contained data.
 *  -1  Capacity or sector read was unavailable.
 */
static int setup_sector_is_zero(const uint8_t *sector)
{
    unsigned int i;

    for (i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (sector[i] != 0U) {
            return 0;
        }
    }

    return 1;
}

static int setup_check_planned_write_sectors(uint32_t sectors)
{
    uint8_t sector[ATA_SECTOR_SIZE];
    uint32_t lba;

    /* The highest planned sector is LBA 319. */
    if (sectors < 320U) {
        return -1;
    }

    /* The boot sector would be overwritten. */
    if (!ata_read_sector(0U, sector)) {
        return -1;
    }

    if (!setup_sector_is_zero(sector)) {
        return 0;
    }

    /* The second-stage loader occupies LBAs 32 through 47. */
    for (lba = 32U; lba <= 47U; lba++) {
        if (!ata_read_sector(lba, sector)) {
            return -1;
        }

        if (!setup_sector_is_zero(sector)) {
            return 0;
        }
    }

    /* The padded installed kernel occupies LBAs 64 through 319. */
    for (lba = 64U; lba <= 319U; lba++) {
        if (!ata_read_sector(lba, sector)) {
            return -1;
        }

        if (!setup_sector_is_zero(sector)) {
            return 0;
        }
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
    const uint8_t *actual
)
{
    unsigned int i;

    for (i = 0; i < ATA_SECTOR_SIZE; i++) {
        if (expected[i] != actual[i]) {
            return 0;
        }
    }

    return 1;
}

static int installer_write_verified_sector(
    uint32_t lba,
    const uint8_t *data
)
{
    uint8_t verify[ATA_SECTOR_SIZE];

    if (!ata_write_sector(lba, data)) {
        terminal_write("WRITE FAILED at LBA ");
        setup_print_u32(lba);
        terminal_writeline("");
        return 0;
    }

    if (!ata_read_sector(lba, verify)) {
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

/*
 * The installation routine deliberately never writes LBAs 1-18.
 *
 * It writes the second-stage loader and kernel first, verifies them,
 * and writes the boot sector last so a partial installation is less
 * likely to become accidentally bootable.
 */
static void installer_install_target(
    const ata_device_info_t *info
)
{
    uint8_t sector[ATA_SECTOR_SIZE];
    uint32_t lba;
    uint32_t i;

    terminal_clear();
    terminal_writeline("NPT-DOS INSTALLER");
    terminal_writeline("-----------------");
    terminal_writeline("");
    terminal_writeline("Performing final read-only checks...");

    if (info == 0 ||
        !info->detected ||
        info->addressable_sectors < 320U) {
        terminal_writeline("SAFETY STOP: Target capacity is insufficient.");
        goto failed;
    }

    /*
     * Recheck NPTFS immediately before writing.
     */
    if (!ata_read_sector(1U, sector)) {
        terminal_writeline("SAFETY STOP: Cannot read LBA 1.");
        goto failed;
    }

    if (setup_has_nptfs_signature(sector)) {
        terminal_writeline("SAFETY STOP: NPTFS signature detected.");
        terminal_writeline("Existing NPTFS data will not be overwritten.");
        goto failed;
    }

    /*
     * Be conservative: require LBAs 1-18 to be zero too.
     * This is not a whole-disk emptiness guarantee.
     */
    for (lba = 1U; lba <= 18U; lba++) {
        if (!ata_read_sector(lba, sector)) {
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

    /*
     * Recheck every sector that the installer will overwrite.
     */
    if (setup_check_planned_write_sectors(
            info->addressable_sectors) != 1) {
        terminal_writeline("SAFETY STOP: Reserved-sector check failed.");
        terminal_writeline("Target was not approved for installation.");
        goto failed;
    }

    terminal_writeline("Preflight checks passed.");
    terminal_writeline("");
    terminal_writeline("Writing second-stage loader...");

    for (i = 0; i < INSTALLER_STAGE2_SECTORS; i++) {
        if (!installer_write_verified_sector(
                INSTALLER_STAGE2_LBA + i,
                installer_stage2_start + i * ATA_SECTOR_SIZE)) {
            goto failed;
        }
    }

    terminal_writeline("Second-stage loader verified.");
    terminal_writeline("Writing installed kernel...");

    for (i = 0; i < INSTALLER_KERNEL_SECTORS; i++) {
        if (!installer_write_verified_sector(
                INSTALLER_KERNEL_LBA + i,
                installer_kernel_start + i * ATA_SECTOR_SIZE)) {
            goto failed;
        }
    }

    terminal_writeline("Installed kernel verified.");
    terminal_writeline("Writing boot sector last...");

    if (!installer_write_verified_sector(
            INSTALLER_BOOT_LBA,
            installer_boot_start)) {
        goto failed;
    }

    terminal_writeline("");
    terminal_writeline("INSTALLATION COMPLETED.");
    terminal_writeline("All written sectors passed read-back verification.");
    terminal_writeline("");
    terminal_writeline("Power off your computer, detach the installer ISO,");
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
    const ata_device_info_t *info
)
{
    uint8_t sector[ATA_SECTOR_SIZE];
    uint32_t sectors;
    uint32_t mib_whole;
    uint32_t mib_fraction;
    int read_ok;
    int overwrite_check;

    terminal_clear();
    terminal_writeline("NPT-DOS SETUP: TARGET INSPECTION");
    terminal_writeline("--------------------------------");
    terminal_writeline("");
    terminal_writeline("Selected target: Primary Master");

    terminal_write("Model: ");
    terminal_writeline(
        info->model[0] != '\0' ? info->model : "(not reported)"
    );
    terminal_writeline("");

    sectors = info->addressable_sectors;

    if (sectors == 0U) {
        terminal_writeline("Capacity: Unknown");
        terminal_writeline("SAFETY STOP: Capacity unavailable.");
        terminal_writeline("Target inspection cannot continue.");
        terminal_writeline("");
        terminal_writeline("No disk writes were performed.");
        terminal_writeline("Press any key to return.");
        setup_wait_for_key();
        return;
    }

    terminal_write("Reported LBA28 sectors: ");

    if (info->lba28_sectors == 0U) {
        terminal_writeline("Not reported");
    } else {
        setup_print_u32(info->lba28_sectors);
        terminal_writeline("");
    }

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

    read_ok = ata_read_sector(
        NPTFS_SUPERBLOCK_LBA,
        sector
    );

    if (!read_ok) {
        terminal_writeline("NPTFS status: Cannot read sector 1.");
        terminal_writeline("SAFETY STOP: Read failure.");
    } else if (setup_has_nptfs_signature(sector)) {
        terminal_writeline("NPTFS status: Signature detected.");
        terminal_writeline("Existing NPTFS data may be present.");
        terminal_writeline("The filesystem will not be formatted.");
    } else {
        terminal_writeline("NPTFS status: Signature not detected.");
        terminal_writeline("This does not prove the disk is empty.");
    }

    terminal_writeline("");
    terminal_writeline("Checking planned overwrite sectors...");

    overwrite_check = setup_check_planned_write_sectors(sectors);

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
            !setup_has_nptfs_signature(sector) &&
            setup_sector_is_zero(sector) &&
            overwrite_check == 1 &&
            info->addressable_sectors >= 320U) {

            char key;

            terminal_writeline("");
            terminal_writeline("WARNING: Installation will write disk sectors.");
            terminal_writeline("A clean scan does not prove the entire disk is empty.");
            terminal_writeline("Use only a new, disposable hard disk if provided.");
            terminal_writeline("");
            terminal_writeline("Press I to continue to confirmation.");
            terminal_writeline("Press any other key to cancel.");

            key = keyboard_getchar();

            if (key == 'i' || key == 'I') {
                terminal_clear();
                terminal_writeline("FINAL INSTALLATION CONFIRMATION");
                terminal_writeline("--------------------------------");
                terminal_writeline("");
                terminal_writeline("Target: Primary Master ATA disk");
                terminal_writeline("Will overwrite LBAs 0, 32-47, and 64-319.");
                terminal_writeline("Will not write LBAs 1-18.");
                terminal_writeline("");
                terminal_writeline("Type Y to authorize installation.");
                terminal_writeline("Any other key cancels.");

                key = keyboard_getchar();

                if (key == 'y' || key == 'Y') {
                    installer_install_target(info);
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
    ata_device_info_t info;

    for (;;) {
        char key;

        terminal_clear();
        terminal_writeline("NPT-DOS SETUP: SELECT TARGET");
        terminal_writeline("----------------------------");
        terminal_writeline("");
        terminal_writeline("Detecting supported ATA device...");
        terminal_writeline("");

        if (!ata_initialize() || !ata_get_device_info(&info)) {
            terminal_writeline("No supported primary-master ATA disk");
            terminal_writeline("was detected by the current driver.");
            terminal_writeline("");
            terminal_writeline("No disk writes were performed.");
            terminal_writeline("");
            terminal_writeline("Press any key to return to setup.");
            setup_wait_for_key();
            return;
        }

        terminal_writeline("Detected target:");
        terminal_writeline("");
        terminal_writeline("  1. Primary Master");
        terminal_write("     Model: ");
        terminal_writeline(
            info.model[0] != '\0' ? info.model : "(not reported)"
        );

        terminal_write("     Addressable sectors: ");
        setup_print_u32(info.addressable_sectors);
        terminal_writeline("");

        terminal_writeline("");
        terminal_writeline("Press 1 to inspect this target.");
        terminal_writeline("Press Q to return to the main setup menu.");

        key = keyboard_getchar();

        if (key == '1') {
            setup_inspect_selected_disk(&info);
        } else if (key == 'q' || key == 'Q') {
            return;
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
