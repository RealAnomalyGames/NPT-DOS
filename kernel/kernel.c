#include <stddef.h>
#include "fs.h"
#include "keyboard.h"
#include "terminal.h"
#include "ata.h"

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

static void print_unsigned(unsigned int value)
{
    char digits[12];
    int count = 0;

    if (value == 0) {
        terminal_putchar('0');
        return;
    }

    while (value > 0 && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (count > 0) {
        terminal_putchar(digits[--count]);
    }
}

static void command_help(void)
{
    terminal_writeline("NPT-DOS Build 008 Commands");
    terminal_writeline("--------------------------");
    terminal_writeline("HELP                    List commands");
    terminal_writeline("VER                     Display version");
    terminal_writeline("CLS                     Clear screen");
    terminal_writeline("ECHO <text>             Display text");
    terminal_writeline("INFO                    System information");
    terminal_writeline("EXIT                    Display exit information");
    terminal_writeline("DIR                     List files");
    terminal_writeline("TOUCH <name>            Create an empty file");
    terminal_writeline("WRITE <name> <text>     Write file contents");
    terminal_writeline("TYPE <name>             Display file contents");
    terminal_writeline("DEL <name>              Delete a file");
    terminal_writeline("COPY <source> <dest>    Copy a file");
    terminal_writeline("REN <old> <new>         Rename a file");
    terminal_writeline("SIZE <name>             Display file size");
    terminal_writeline("FSINFO                  Filesystem information");
    terminal_writeline("FORMAT CONFIRM          Format the NPTFS disk");
    terminal_writeline("");
    terminal_writeline("WARNING: Formatting erases NPTFS data.");
}

static void command_ver(void)
{
    terminal_writeline("NPT-DOS Build 008");
    terminal_writeline("32-bit x86 operating system");
    terminal_writeline("Neptune Corporation");
}

static void command_info(void)
{
    terminal_writeline("NPT-DOS System Information");
    terminal_writeline("--------------------------");
    terminal_writeline("Architecture: 32-bit x86");
    terminal_writeline("Interface: VGA text terminal");
    terminal_writeline("Input: PS/2 keyboard");
    terminal_writeline("Bootloader: GRUB");
    terminal_writeline("Filesystem: NPTFS v1");
    terminal_writeline("Storage: IDE hard disk");
}

static void command_dir(void)
{
    int count = fs_file_count();
    int i;

    terminal_writeline("NPTFS Directory");
    terminal_writeline("----------------");

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    if (count == 0) {
        terminal_writeline("No files found.");
    } else {
        for (i = 0; i < count; i++) {
            const char *name = fs_filename_at(i);

            if (name != NULL) {
                int size = fs_file_size(name);

                terminal_write(name);
                terminal_write("  ");

                if (size >= 0) {
                    print_unsigned((unsigned int)size);
                } else {
                    terminal_write("?");
                }

                terminal_writeline(" bytes");
            }
        }
    }

    terminal_write("Files: ");
    print_unsigned((unsigned int)count);
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
        case FS_NOT_READY:
            terminal_writeline("Error: No formatted NPTFS disk is ready.");
            break;
        case FS_IO_ERROR:
            terminal_writeline("Error: Disk read/write operation failed.");
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

static void command_copy(char *arguments)
{
    char *source = take_token(&arguments);
    char *destination = take_token(&arguments);
    const char *contents;
    fs_result_t result;

    arguments = skip_spaces(arguments);

    if (*source == '\0' || *destination == '\0' ||
        *arguments != '\0') {
        terminal_writeline("Usage: COPY <source> <destination>");
        return;
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    contents = fs_read(source);

    if (contents == NULL) {
        terminal_writeline("Error: Source file could not be read.");
        return;
    }

    result = fs_create(destination);

    if (result != FS_OK) {
        report_fs_result(result);
        return;
    }

    result = fs_write(destination, contents);

    if (result != FS_OK) {
        (void)fs_delete(destination);
        report_fs_result(result);
        return;
    }

    terminal_writeline("File copied successfully.");
}

static void command_rename(char *arguments)
{
    char *old_name = take_token(&arguments);
    char *new_name = take_token(&arguments);
    const char *contents;
    fs_result_t result;

    arguments = skip_spaces(arguments);

    if (*old_name == '\0' || *new_name == '\0' ||
        *arguments != '\0') {
        terminal_writeline("Usage: REN <old_name> <new_name>");
        return;
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    contents = fs_read(old_name);

    if (contents == NULL) {
        terminal_writeline("Error: Source file could not be read.");
        return;
    }

    result = fs_create(new_name);

    if (result != FS_OK) {
        report_fs_result(result);
        return;
    }

    result = fs_write(new_name, contents);

    if (result != FS_OK) {
        (void)fs_delete(new_name);
        report_fs_result(result);
        return;
    }

    result = fs_delete(old_name);

    if (result != FS_OK) {
        terminal_writeline("Warning: New file was created,");
        terminal_writeline("but the original could not be deleted.");
        report_fs_result(result);
        return;
    }

    terminal_writeline("File renamed successfully.");
}

static void command_size(char *arguments)
{
    char *name = take_token(&arguments);
    int size;

    arguments = skip_spaces(arguments);

    if (*name == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: SIZE <name>");
        return;
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    size = fs_file_size(name);

    if (size < 0) {
        terminal_writeline("Error: File not found.");
        return;
    }

    terminal_write("File: ");
    terminal_writeline(name);
    terminal_write("Size: ");
    print_unsigned((unsigned int)size);
    terminal_writeline(" bytes");
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
        terminal_writeline("NPTFS Filesystem Information");
        terminal_writeline("----------------------------");

        if (!fs_is_ready()) {
            terminal_writeline("Status: Not ready");
            terminal_writeline("Use FORMAT CONFIRM on the dedicated test disk.");
        } else {
            terminal_writeline("Status: Ready");
            terminal_writeline("Storage: IDE hard disk");
            terminal_writeline("Format: NPTFS v1");
            terminal_writeline("Maximum files: 16");
            terminal_writeline("Maximum filename: 15 characters");
            terminal_writeline("Maximum file contents: 255 bytes");
        }

    } else if (string_equals_ignore_case(command, "TOUCH")) {
        char *name = take_token(&arguments);

        arguments = skip_spaces(arguments);

        if (*name == '\0' || *arguments != '\0') {
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

        arguments = skip_spaces(arguments);

        if (*name == '\0' || *arguments != '\0') {
            terminal_writeline("Usage: TYPE <name>");
        } else {
            contents = fs_read(name);

            if (contents == NULL) {
                terminal_writeline("Error: File could not be read.");
            } else {
                terminal_writeline(contents);
            }
        }

    } else if (string_equals_ignore_case(command, "DEL")) {
        char *name = take_token(&arguments);

        arguments = skip_spaces(arguments);

        if (*name == '\0' || *arguments != '\0') {
            terminal_writeline("Usage: DEL <name>");
        } else {
            report_fs_result(fs_delete(name));
        }

    } else if (string_equals_ignore_case(command, "COPY")) {
        command_copy(arguments);

    } else if (string_equals_ignore_case(command, "REN")) {
        command_rename(arguments);

    } else if (string_equals_ignore_case(command, "SIZE")) {
        command_size(arguments);

    } else if (string_equals_ignore_case(command, "FORMAT")) {
        if (!string_equals_ignore_case(arguments, "CONFIRM")) {
            terminal_writeline("Formatting erases all NPTFS files.");
            terminal_writeline("To proceed, enter: FORMAT CONFIRM");
            return;
        }

        terminal_writeline("Starting NPTFS format...");

        {
            fs_result_t result = fs_format();
            report_fs_result(result);

            if (fs_is_ready()) {
                terminal_writeline("NPTFS format completed.");
            } else {
                terminal_writeline("NPTFS remains unavailable.");
            }
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

    terminal_writeline("NPT-DOS Build 008");
    terminal_writeline("Neptune Corporation");
    terminal_writeline("--------------------------------");

    if (fs_is_ready()) {
        terminal_writeline("NPTFS persistent storage initialized.");
    } else {
        terminal_writeline("NPTFS is not formatted or unavailable.");
        terminal_writeline("Use FORMAT CONFIRM on the dedicated test disk.");
    }

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
