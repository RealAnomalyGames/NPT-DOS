#include <stddef.h>
#include "keyboard.h"
#include "terminal.h"

#define INPUT_BUFFER_SIZE 128

#define NPT_DOS_VERSION "NPT-DOS Build 005"

/* Compare two strings without considering letter case. */
static int string_equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;

        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }

        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }

        if (ca != cb) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

/* Print the available commands. */
static void command_help(void)
{
    terminal_writeline("Available NPT-DOS commands:");
    terminal_writeline("");
    terminal_writeline("HELP         List available commands");
    terminal_writeline("VER          Display the OS version");
    terminal_writeline("CLS          Clear the terminal");
    terminal_writeline("ECHO <text>  Display text");
    terminal_writeline("INFO         Display system information");
    terminal_writeline("EXIT         Display exit information");
}

/* Display version information. */
static void command_ver(void)
{
    terminal_writeline("NPT-DOS");
    terminal_writeline("Version: Build 005");
    terminal_writeline("Architecture: 32-bit x86");
    terminal_writeline("Platform: PC-compatible");
    terminal_writeline("Copyright (c) Neptune Corporation");
}

/* Display basic OS information. */
static void command_info(void)
{
    terminal_writeline("NPT-DOS System Information");
    terminal_writeline("---------------------------");
    terminal_writeline("Operating system: NPT-DOS");
    terminal_writeline("Architecture: 32-bit x86");
    terminal_writeline("Interface: Text command line");
    terminal_writeline("Terminal: VGA text mode");
    terminal_writeline("Input: PS/2 keyboard");
    terminal_writeline("Bootloader: GRUB");
}

/* Execute a command and its optional arguments. */
static void execute_command(char *line)
{
    char *command;
    char *arguments;

    /* Ignore leading spaces. */
    while (*line == ' ' || *line == '\t') {
        line++;
    }

    /* Ignore empty commands. */
    if (*line == '\0') {
        return;
    }

    command = line;

    /* Separate the command from its arguments. */
    while (*line != '\0' && *line != ' ' && *line != '\t') {
        line++;
    }

    if (*line != '\0') {
        *line = '\0';
        line++;

        while (*line == ' ' || *line == '\t') {
            line++;
        }
    }

    arguments = line;

    if (string_equals_ignore_case(command, "HELP")) {
        command_help();
    }
    else if (string_equals_ignore_case(command, "VER")) {
        command_ver();
    }
    else if (string_equals_ignore_case(command, "CLS")) {
        terminal_clear();
    }
    else if (string_equals_ignore_case(command, "ECHO")) {
        terminal_writeline(arguments);
    }
    else if (string_equals_ignore_case(command, "INFO")) {
        command_info();
    }
    else if (string_equals_ignore_case(command, "EXIT")) {
        terminal_writeline(
            "NPT-DOS runs directly on the machine."
        );
        terminal_writeline(
            "There is no host command prompt to return to."
        );
    }
    else {
        terminal_write("Unknown command: ");
        terminal_writeline(command);
        terminal_writeline("Type HELP to see available commands.");
    }
}

void kernel_main(void)
{
    char input[INPUT_BUFFER_SIZE];

    terminal_initialize();
    keyboard_initialize();

    terminal_writeline("NPT-DOS Build 005");
    terminal_writeline("Neptune Corporation");
    terminal_writeline("--------------------------------");
    terminal_writeline("Command interpreter initialized.");
    terminal_writeline("Type HELP to list available commands.");
    terminal_writeline("");

    for (;;) {
        size_t length = 0;

        terminal_write("NPT-DOS> ");

        for (;;) {
            char c = keyboard_getchar();

            if (c == '\n') {
                input[length] = '\0';
                terminal_writeline("");
                execute_command(input);
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

        /* Separate command output from the next prompt. */
        terminal_writeline("");
    }
}
