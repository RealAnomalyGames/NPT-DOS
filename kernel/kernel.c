#include "terminal.h"

void kernel_main(void)
{
    terminal_initialize();

    terminal_writeline("NPT-DOS Build 003");
    terminal_writeline("Neptune Corporation");
    terminal_writeline("--------------------------------");
    terminal_writeline("");
    terminal_writeline("Terminal initialized.");
    terminal_writeline("Grey text mode enabled.");
    terminal_writeline("");
    terminal_write("NPT-DOS>");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
