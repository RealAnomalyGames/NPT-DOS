#include <stddef.h>
#include "fs.h"
#include "keyboard.h"
#include "terminal.h"

#define INPUT_BUFFER_SIZE 128

static int string_equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;

        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';

        if (ca != cb) return 0;

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void command_help(void)
{
    terminal_writeline("NPT-DOS Build 006 Commands");
    terminal_writeline("--------------------------");
    terminal_writeline("HELP              List commands");
    terminal_writeline("VER               Display version");
    terminal_writeline("CLS               Clear screen");
    terminal_writeline("ECHO <text>       Display text");
    terminal_writeline("INFO              System information");
    terminal_writeline("EXIT              Display exit information");
    terminal_writeline("DIR               List files");
    terminal_writeline("TOUCH <name>      Create an empty file");
    terminal_writeline("WRITE <name> <text>  Write or replace file text");
    terminal_writeline("TYPE <name>       Display file contents");
    terminal_writeline("DEL <name>        Delete a file");
    terminal_writeline("FSINFO            Filesystem information");
    terminal_writeline("");
    terminal_writeline("Note: RAM files are lost on reboot.");
}

static void command_ver(void)
{
    terminal_writeline("NPT-DOS Build 006");
    terminal_writeline("32-bit x86 operating system");
    terminal_writeline("Neptune Corporation");
}

static void command_info(void)
{
    terminal_writeline("NPT-DOS System Information");
    terminal_writeline("---------------------------");
    terminal_writeline("Architecture: 32-bit x86");
    terminal_writeline("Interface: VGA text terminal");
    terminal_writeline("Input: PS/2 keyboard");
    terminal_writeline("Bootloader: GRUB");
    terminal_writeline("Filesystem: NPTFS RAM filesystem");
}

static void command_dir(void)
{
    int count = fs_file_count();
    int i;

    terminal_writeline("NPTFS Directory");
    terminal_writeline("----------------");

    if (count == 0) {
        terminal_writeline("No files found.");
    } else {
        for (i = 0; i < count; i++) {
            const char *name = fs_filename_at(i);

            if (name != NULL) {
                terminal_write(name);
                terminal_write("  ");

                {
                    int size = fs_file_size(name);
                    char digits[12];
                    int n = 0;

                    if (size == 0) {
                        terminal_write("0");
                    } else {
                        while (size > 0 && n < 11) {
                            digits[n++] = (char)('0' + size % 10);
                            size /= 10;
                        }

                        while (n > 0) {
                            terminal_putchar(digits[--n]);
                        }
                    }
                }

                terminal_writeline(" bytes");
            }
        }
    }

    terminal_write("Files: ");
    {
        char digit = (char)('0' + count);
        terminal_putchar(digit);
    }
    terminal_writeline("");
}

static void report_fs_result(fs_result_t result)
{
    switch (result) {
        case FS_OK:
            terminal_writeline("Operation completed.");
            break;
        case FS_NOT_FOUND:
            terminal_writeline("Error: File not found.");
            break;
        case FS_ALREADY_EXISTS:
            terminal_writeline("Error: File already exists.");
            break;
        case FS_FULL:
            terminal_writeline("Error: Filesystem is full.");
            break;
        case FS_INVALID_NAME:
            terminal_writeline("Error: Invalid filename.");
            break;
        case FS_TOO_LARGE:
            terminal_writeline("Error: Text exceeds 255 characters.");
            break;
    }
}

static char *skip_spaces(char *text)
{
    while (*text == ' ' || *text == '\t') {
        text++;
    }

    return text;
}

static char *take_token(char **text)
{
    char *token = skip_spaces(*text);
    char *end = token;

    while (*end != '\0' && *end != ' ' && *end != '\t') {
        end++;
    }

    if (*end != '\0') {
        *end = '\0';
        *text = end + 1;
    } else {
        *text = end;
    }

    return token;
}

static void execute_command(char *line)
{
    char *arguments = skip_spaces(line);
    char *command = take_token(&arguments);

    arguments = skip_spaces(arguments);

    if (*command == '\0') return;

    if (string_equals_ignore_case(command, "HELP")) {
        command_help();
    } else if (string_equals_ignore_case(command, "VER")) {
        command_ver();
    } else if (string_equals_ignore_case(command, "CLS")) {
        terminal_clear();
    } else if (string_equals_ignore_case(command, "ECHO")) {
        terminal_writeline(arguments);
    } else if (string_equals_ignore_case(command, "INFO")) {
        command_info();
    } else if (string_equals_ignore_case(command, "EXIT")) {
        terminal_writeline("NPT-DOS runs directly on this machine.");
        terminal_writeline("There is no host prompt to return to.");
    } else if (string_equals_ignore_case(command, "DIR")) {
        command_dir();
    } else if (string_equals_ignore_case(command, "FSINFO")) {
        terminal_writeline("NPTFS RAM Filesystem");
        terminal_write("File slots: ");
        terminal_writeline("16");
        terminal_write("Used slots: ");

        {
            int count = fs_file_count();
            terminal_putchar((char)('0' + count));
        }

        terminal_writeline("");
        terminal_writeline("Maximum filename length: 15");
        terminal_writeline("Maximum file size: 255 bytes");
        terminal_writeline("Storage: volatile RAM");
    } else if (string_equals_ignore_case(command, "TOUCH")) {
        char *name = take_token(&arguments);

        if (*name == '\0') {
            terminal_writeline("Usage: TOUCH <name>");
        } else {
            report_fs_result(fs_create(name));
        }
    } else if (string_equals_ignore_case(command, "WRITE")) {
        char *name = take_token(&arguments);
        char *text = skip_spaces(arguments);

        if (*name == '\0' || *text == '\0') {
            terminal_writeline("Usage: WRITE <name> <text>");
        } else {
            report_fs_result(fs_write(name, text));
        }
    } else if (string_equals_ignore_case(command, "TYPE")) {
        char *name = take_token(&arguments);
        const char *contents;

        if (*name == '\0') {
            terminal_writeline("Usage: TYPE <name>");
        } else {
            contents = fs_read(name);

            if (contents == NULL) {
                terminal_writeline("Error: File not found.");
            } else {
                terminal_writeline(contents);
            }
        }
    } else if (string_equals_ignore_case(command, "DEL")) {
        char *name = take_token(&arguments);

        if (*name == '\0') {
            terminal_writeline("Usage: DEL <name>");
        } else {
            report_fs_result(fs_delete(name));
        }
    } else {
        terminal_write("Unknown command: ");
        terminal_writeline(command);
        terminal_writeline("Type HELP for commands.");
    }
}

void kernel_main(void)
{
    char input[INPUT_BUFFER_SIZE];

    terminal_initialize();
    keyboard_initialize();
    fs_initialize();

    terminal_writeline("NPT-DOS Build 006");
    terminal_writeline("Neptune Corporation");
    terminal_writeline("--------------------------------");
    terminal_writeline("NPTFS RAM filesystem initialized.");
    terminal_writeline("Type HELP to list commands.");
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

            if (c >= 32 && c <= 126 &&
                length < INPUT_BUFFER_SIZE - 1) {
                input[length++] = c;
                terminal_putchar(c);
            }
        }

        terminal_writeline("");
    }
}
