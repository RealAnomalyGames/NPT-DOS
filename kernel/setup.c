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
    terminal_writeline("                 NPT-DOS SETUP                  ");
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

    terminal_writeline("");
    terminal_writeline("INSTALLATION: DISABLED");
    terminal_writeline("This is a read-only inspection.");
    terminal_writeline("No disk writes were performed.");
    terminal_writeline("");
    terminal_writeline("Press any key to return.");

    setup_wait_for_key();
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
            terminal_writeline("Check the VirtualBox storage settings.");
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
