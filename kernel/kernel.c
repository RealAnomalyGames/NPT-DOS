#include <stddef.h>
#include "keyboard.h"
#include "terminal.h"

#define INPUT_BUFFER_SIZE 128

static void process_line(const char *line)
{
    terminal_writeline("");
    terminal_write("Input received: ");
    terminal_writeline(line);
}

void kernel_main(void)
{
    terminal_initialize();
    keyboard_initialize();

    terminal_writeline("NPT-DOS Build 004");
    terminal_writeline("Neptune Corporation");
    terminal_writeline("--------------------------------");
    terminal_writeline("Keyboard input initialized.");
    terminal_writeline("");

    char input[INPUT_BUFFER_SIZE];

    for (;;) {
        size_t length = 0;

        terminal_write("NPT-DOS> ");

        for (;;) {
            char c = keyboard_getchar();

            if (c == '\n') {
                input[length] = '\0';
                process_line(input);
                break;
            }

            if (c == '\b') {
                if (length > 0) {
                    length--;
                    terminal_backspace();
                }

                continue;
            }

            if (c >= 32 && c <= 126) {
                if (length < INPUT_BUFFER_SIZE - 1) {
                    input[length++] = c;
                    terminal_putchar(c);
                }
            }
        }
    }
}
