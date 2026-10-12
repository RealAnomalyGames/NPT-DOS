#include "setup.h"
#include "terminal.h"
#include "keyboard.h"

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
    terminal_writeline("|    Install NPT-DOS onto a supported disk.    |");
    terminal_writeline("|    Installation is planned for Build 014.    |");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("| 2. PORTABLE MODE                             |");
    terminal_writeline("|                                              |");
    terminal_writeline("|    Continue booting from the current media. |");
    terminal_writeline("|    No installer formatting or partitioning. |");
    terminal_writeline("+----------------------------------------------+");
    terminal_writeline("");
    terminal_writeline("Press 1 or 2 to select an option.");
    terminal_writeline("Press Q to exit setup.");
    terminal_writeline("");
}

static void setup_wait_for_key(void)
{
    (void)keyboard_getchar();
}

int setup_run(void)
{
    for (;;) {
        char key;

        setup_draw_screen();

        key = keyboard_getchar();

        if (key == '1') {
            terminal_clear();
            terminal_writeline("NPT-DOS Setup: Hard Disk Installation");
            terminal_writeline("-------------------------------------");
            terminal_writeline("");
            terminal_writeline("The installer is not available in Build 013.");
            terminal_writeline("Hard disk installation is planned for Build 014.");
            terminal_writeline("");
            terminal_writeline("No disk has been formatted or modified.");
            terminal_writeline("");
            terminal_writeline("Press any key to return to setup.");

            setup_wait_for_key();
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
