#include <stddef.h>
#include "fs.h"
#include "keyboard.h"
#include "terminal.h"
#include "ata.h"

#define INPUT_BUFFER_SIZE 128
#define APPEND_BUFFER_SIZE (FS_CONTENT_MAX + 1)
#define HEAD_DISPLAY_LENGTH 80

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

static char to_upper_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }

    return c;
}

static int find_substring_ignore_case(const char *text,
                                      const char *search)
{
    int position = 0;
    int offset;

    if (*search == '\0') return 0;

    while (*text != '\0') {
        offset = 0;

        while (search[offset] != '\0' &&
               text[offset] != '\0' &&
               to_upper_char(text[offset]) ==
               to_upper_char(search[offset])) {
            offset++;
        }

        if (search[offset] == '\0') {
            return position;
        }

        text++;
        position++;
    }

    return -1;
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
    terminal_writeline("NPT-DOS Build 009 Commands");
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
    terminal_writeline("COPY <src> <dest>       Copy a file");
    terminal_writeline("REN <old> <new>         Rename a file");
    terminal_writeline("SIZE <name>             Display file size");
    terminal_writeline("APPEND <name> <text>    Append text to a file");
    terminal_writeline("HEAD <name>             Display first 80 characters");
    terminal_writeline("FIND <name> <text>      Search file contents");
    terminal_writeline("CLEAR <name>            Empty a file");
    terminal_writeline("REPLACE <name> <text>   Replace file contents");
    terminal_writeline("FSINFO                  Filesystem information");
    terminal_writeline("FORMAT CONFIRM          Format the NPTFS disk");
    terminal_writeline("");
    terminal_writeline("WARNING: Formatting erases NPTFS data.");
}

static void command_ver(void)
{
    terminal_writeline("NPT-DOS Build 009");
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

static void command_append(char *arguments)
{
    char *name = take_token(&arguments);
    char *text = skip_spaces(arguments);
    const char *contents;
    char combined[APPEND_BUFFER_SIZE];
    int existing_size;
    size_t text_length = 0;
    size_t total_length;
    size_t i;
    int add_separator = 0;

    if (*name == '\0' || *text == '\0') {
        terminal_writeline("Usage: APPEND <name> <text>");
        return;
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    existing_size = fs_file_size(name);

    if (existing_size < 0) {
        terminal_writeline("Error: File not found.");
        return;
    }

    contents = fs_read(name);

    if (contents == NULL) {
        terminal_writeline("Error: File could not be read.");
        return;
    }

    /*
     * Copy fs_read's shared buffer before doing any further
     * filesystem operations.
     */
    for (i = 0; i < (size_t)existing_size; i++) {
        combined[i] = contents[i];
    }

    while (text[text_length] != '\0') {
        text_length++;
    }

    total_length = (size_t)existing_size;

    if (total_length > 0 && text_length > 0 &&
        combined[total_length - 1] != ' ' &&
        combined[total_length - 1] != '\t' &&
        text[0] != ' ' && text[0] != '\t') {
        add_separator = 1;
    }

    if (total_length + (size_t)add_separator + text_length >
        FS_CONTENT_MAX) {
        terminal_writeline("Error: Append would exceed 255 bytes.");
        return;
    }

    if (add_separator) {
        combined[total_length++] = ' ';
    }

    for (i = 0; i < text_length; i++) {
        combined[total_length++] = text[i];
    }

    combined[total_length] = '\0';

    report_fs_result(fs_write(name, combined));
}

static void command_head(char *arguments)
{
    char *name = take_token(&arguments);
    const char *contents;
    char output[HEAD_DISPLAY_LENGTH + 1];
    int size;
    size_t length;
    size_t i;

    arguments = skip_spaces(arguments);

    if (*name == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: HEAD <name>");
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

    contents = fs_read(name);

    if (contents == NULL) {
        terminal_writeline("Error: File could not be read.");
        return;
    }

    length = (size_t)size;

    if (length > HEAD_DISPLAY_LENGTH) {
        length = HEAD_DISPLAY_LENGTH;
    }

    for (i = 0; i < length; i++) {
        output[i] = contents[i];
    }

    output[length] = '\0';

    terminal_writeline("Beginning of file:");
    terminal_writeline(output);

    if (size > HEAD_DISPLAY_LENGTH) {
        terminal_writeline("[Output limited to 80 characters.]");
    }
}

static void command_find(char *arguments)
{
    char *name = take_token(&arguments);
    char *search = skip_spaces(arguments);
    const char *contents;
    int position;

    if (*name == '\0' || *search == '\0') {
        terminal_writeline("Usage: FIND <name> <text>");
        return;
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    contents = fs_read(name);

    if (contents == NULL) {
        terminal_writeline("Error: File could not be read.");
        return;
    }

    position = find_substring_ignore_case(contents, search);

    if (position < 0) {
        terminal_writeline("Text not found.");
    } else {
        terminal_write("Text found at character position: ");
        print_unsigned((unsigned int)(position + 1));
        terminal_writeline("");
    }
}

static void command_clear(char *arguments)
{
    char *name = take_token(&arguments);
    int size;

    arguments = skip_spaces(arguments);

    if (*name == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: CLEAR <name>");
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

    report_fs_result(fs_write(name, ""));
}

static void command_replace(char *arguments)
{
    char *name = take_token(&arguments);
    char *text = skip_spaces(arguments);
    int size;

    if (*name == '\0' || *text == '\0') {
        terminal_writeline("Usage: REPLACE <name> <text>");
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

    report_fs_result(fs_write(name, text));
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

    } else if (string_equals_ignore_case(command, "APPEND")) {
        command_append(arguments);

    } else if (string_equals_ignore_case(command, "HEAD")) {
        command_head(arguments);

    } else if (string_equals_ignore_case(command, "FIND")) {
        command_find(arguments);

    } else if (string_equals_ignore_case(command, "CLEAR")) {
        command_clear(arguments);

    } else if (string_equals_ignore_case(command, "REPLACE")) {
        command_replace(arguments);

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

    terminal_writeline("NPT-DOS Build 009");
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
