#include <stdint.h>
#include <stddef.h>
#include "fs.h"
#include "keyboard.h"
#include "terminal.h"
#include "ata.h"
#include "timer.h"
#include "rtc.h"
#include "setup.h"

#define INPUT_BUFFER_SIZE 128
#define APPEND_BUFFER_SIZE (FS_CONTENT_MAX + 1)
#define HEAD_DISPLAY_LENGTH 80

#define HISTORY_CAPACITY 10
#define PROMPT_MAX_LENGTH 30

static int verbose_mode = 0;

static char command_history_buffer[HISTORY_CAPACITY][INPUT_BUFFER_SIZE];
static unsigned int history_count = 0;
static unsigned int history_next = 0;

static char shell_prompt[PROMPT_MAX_LENGTH + 1] = "NPT-DOS";

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
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static int find_substring_ignore_case(const char *text, const char *search)
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

        if (search[offset] == '\0') return position;

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

    while (count > 0) terminal_putchar(digits[--count]);
}

static void report_fs_result(fs_result_t result)
{
    if (verbose_mode) {
        terminal_write("[VERBOSE] Filesystem result code: ");
        print_unsigned((unsigned int)result);
        terminal_writeline("");
    }
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
            terminal_writeline("Error: Invalid filename or attribute.");
            break;
        case FS_TOO_LARGE:
            terminal_writeline("Error: Text exceeds 255 characters.");
            break;
        case FS_NOT_READY:
            terminal_writeline("Error: No formatted NPTFS disk is ready.");
            break;
        case FS_READ_ONLY:
            terminal_writeline("Error: File is read-only.");
            break;
        case FS_IO_ERROR:
            terminal_writeline("Error: Disk read/write operation failed.");
            break;
    }
}

static char *skip_spaces(char *text)
{
    while (*text == ' ' || *text == '\t') text++;
    return text;
}

static char *take_token(char **text)
{
    char *token = skip_spaces(*text);
    char *end = token;

    while (*end != '\0' && *end != ' ' && *end != '\t') end++;

    if (*end != '\0') {
        *end = '\0';
        *text = end + 1;
    } else {
        *text = end;
    }

    return token;
}

static int file_is_hidden(const char *name)
{
    int attributes = fs_get_attributes(name);
    return attributes >= 0 && (attributes & FS_ATTR_HIDDEN);
}

static void print_file_attributes(const char *name)
{
    int attributes = fs_get_attributes(name);

    terminal_write(" [");

    if (attributes < 0 || attributes == 0) {
        terminal_write("-");
    } else {
        if (attributes & FS_ATTR_READ_ONLY) terminal_write("R");
        if (attributes & FS_ATTR_HIDDEN) terminal_write("H");
        if (attributes & FS_ATTR_SYSTEM) terminal_write("S");
    }

    terminal_write("]");
}


static void command_help(char *arguments)
{
    char *topic = take_token(&arguments);

    arguments = skip_spaces(arguments);

    if (*topic == '\0') {
        terminal_writeline("   NPT-DOS 1.01 Commands  ");
        terminal_writeline("--------------------------");
        terminal_writeline("HELP [command]          List commands or show command help");
        terminal_writeline("VER                     Display version");
        terminal_writeline("CLS                     Clear screen");
        terminal_writeline("ECHO <text>             Display text");
        terminal_writeline("INFO                    Basic system information");
        terminal_writeline("SYSINFO                 Detailed system information");
        terminal_writeline("MEM                     Memory information");
        terminal_writeline("UPTIME                  Time since boot");
        terminal_writeline("DATE                    Display current date");
        terminal_writeline("TIME                    Display current time");
        terminal_writeline("VERBOSE [ON|OFF]        Toggle verbose mode");
        terminal_writeline("HISTORY                 Show recent commands");
        terminal_writeline("REBOOT                  Restart the machine");
        terminal_writeline("PROMPT [text]           View or change the prompt");
        terminal_writeline("PAUSE                   Wait for a key");
        terminal_writeline("DIR [/A]                List files");
        terminal_writeline("TREE [/A]               Display file tree");
        terminal_writeline("TOUCH <name>            Create an empty file");
        terminal_writeline("WRITE <name> <text>     Write file contents");
        terminal_writeline("TYPE <name>             Display file contents");
        terminal_writeline("DEL <name>              Delete a file");
        terminal_writeline("COPY <src> <dest>       Copy a file");
        terminal_writeline("REN <old> <new>         Rename a file");
        terminal_writeline("RENAME <old> <new>      Rename a file");
        terminal_writeline("SIZE <name>             Display file size");
        terminal_writeline("APPEND <name> <text>    Append text");
        terminal_writeline("HEAD <name>             Display first 80 characters");
        terminal_writeline("FIND <name> <text>      Search file contents");
        terminal_writeline("CLEAR <name>            Empty a file");
        terminal_writeline("REPLACE <name> <text>   Replace file contents");
        terminal_writeline("ATTRIB <name> [+/-RHS]  View or change attributes");
        terminal_writeline("DISKINFO                Disk/filesystem details");
        terminal_writeline("DISKID                  Display ATA model and capacity");
        terminal_writeline("FREE                    Available storage");
        terminal_writeline("FSINFO                  Filesystem information");
        terminal_writeline("FORMAT CONFIRM          Format the NPTFS disk");
        terminal_writeline("EXIT                    Display exit information");
        terminal_writeline("");
        terminal_writeline("Use HELP <command> for details.");
        terminal_writeline("WARNING: FORMAT CONFIRM erases NPTFS data.");
        return;
    }

    if (*arguments != '\0') {
        terminal_writeline("Usage: HELP [command]");
        return;
    }

    if (string_equals_ignore_case(topic, "HELP")) {
        terminal_writeline("HELP [command]");
        terminal_writeline("Lists all commands or displays help for one command.");
    } else if (string_equals_ignore_case(topic, "VERBOSE")) {
        terminal_writeline("VERBOSE [ON|OFF]");
        terminal_writeline("Without an argument, toggles verbose mode.");
        terminal_writeline("ON and OFF explicitly set the mode.");
    } else if (string_equals_ignore_case(topic, "HISTORY")) {
        terminal_writeline("HISTORY");
        terminal_writeline("Shows up to 10 recent commands from this session.");
    } else if (string_equals_ignore_case(topic, "REBOOT")) {
        terminal_writeline("REBOOT");
        terminal_writeline("Requests a hardware reset immediately.");
        terminal_writeline("Shell history and prompt settings are not saved.");
    } else if (string_equals_ignore_case(topic, "PROMPT")) {
        terminal_writeline("PROMPT [text]");
        terminal_writeline("Shows or changes the shell prompt.");
        terminal_writeline("Maximum custom prompt length: 30 characters.");
    } else if (string_equals_ignore_case(topic, "PAUSE")) {
        terminal_writeline("PAUSE");
        terminal_writeline("Waits until a key is pressed.");
    } else if (string_equals_ignore_case(topic, "VER")) {
        terminal_writeline("VER");
        terminal_writeline("Displays the NPT-DOS version and publisher.");
    } else if (string_equals_ignore_case(topic, "CLS")) {
        terminal_writeline("CLS");
        terminal_writeline("Clears the terminal screen.");
    } else if (string_equals_ignore_case(topic, "ECHO")) {
        terminal_writeline("ECHO <text>");
        terminal_writeline("Displays the supplied text.");
    } else if (string_equals_ignore_case(topic, "INFO") ||
               string_equals_ignore_case(topic, "SYSINFO")) {
        terminal_writeline("INFO / SYSINFO");
        terminal_writeline("Displays basic or detailed system information.");
    } else if (string_equals_ignore_case(topic, "MEM")) {
        terminal_writeline("MEM");
        terminal_writeline("Displays available memory information.");
    } else if (string_equals_ignore_case(topic, "UPTIME")) {
        terminal_writeline("UPTIME");
        terminal_writeline("Displays elapsed time since boot.");
    } else if (string_equals_ignore_case(topic, "DATE")) {
        terminal_writeline("DATE");
        terminal_writeline("Displays the RTC date as YYYY-MM-DD.");
    } else if (string_equals_ignore_case(topic, "TIME")) {
        terminal_writeline("TIME");
        terminal_writeline("Displays the RTC time in 24-hour format.");
    } else if (string_equals_ignore_case(topic, "DIR")) {
        terminal_writeline("DIR [/A]");
        terminal_writeline("Lists files; /A includes hidden files.");
    } else if (string_equals_ignore_case(topic, "TREE")) {
        terminal_writeline("TREE [/A]");
        terminal_writeline("Displays the directory tree; /A includes hidden files.");
    } else if (string_equals_ignore_case(topic, "TOUCH")) {
        terminal_writeline("TOUCH <name>");
        terminal_writeline("Creates an empty file.");
    } else if (string_equals_ignore_case(topic, "WRITE")) {
        terminal_writeline("WRITE <name> <text>");
        terminal_writeline("Creates or overwrites a file with text.");
    } else if (string_equals_ignore_case(topic, "TYPE")) {
        terminal_writeline("TYPE <name>");
        terminal_writeline("Displays file contents.");
    } else if (string_equals_ignore_case(topic, "DEL")) {
        terminal_writeline("DEL <name>");
        terminal_writeline("Deletes a file unless it is read-only.");
    } else if (string_equals_ignore_case(topic, "COPY")) {
        terminal_writeline("COPY <source> <destination>");
        terminal_writeline("Copies file contents and attributes.");
    } else if (string_equals_ignore_case(topic, "REN") ||
               string_equals_ignore_case(topic, "RENAME")) {
        terminal_writeline("RENAME <old> <new> (REN is an alias)");
        terminal_writeline("Renames a file without copying its contents.");
    } else if (string_equals_ignore_case(topic, "SIZE")) {
        terminal_writeline("SIZE <name>");
        terminal_writeline("Displays file size in bytes.");
    } else if (string_equals_ignore_case(topic, "APPEND")) {
        terminal_writeline("APPEND <name> <text>");
        terminal_writeline("Adds text to the end of a file.");
    } else if (string_equals_ignore_case(topic, "HEAD")) {
        terminal_writeline("HEAD <name>");
        terminal_writeline("Displays the first 80 characters of a file.");
    } else if (string_equals_ignore_case(topic, "FIND")) {
        terminal_writeline("FIND <name> <text>");
        terminal_writeline("Searches file contents without case sensitivity.");
    } else if (string_equals_ignore_case(topic, "CLEAR")) {
        terminal_writeline("CLEAR <name>");
        terminal_writeline("Empties a file but keeps its name.");
    } else if (string_equals_ignore_case(topic, "REPLACE")) {
        terminal_writeline("REPLACE <name> <text>");
        terminal_writeline("Replaces all file contents.");
    } else if (string_equals_ignore_case(topic, "ATTRIB")) {
        terminal_writeline("ATTRIB <name> [+R|-R|+H|-H|+S|-S]");
        terminal_writeline("R=Read-only, H=Hidden, S=System.");
    } else if (string_equals_ignore_case(topic, "DISKINFO")) {
        terminal_writeline("DISKINFO");
        terminal_writeline("Displays NPTFS storage layout and capacity.");
    } else if (string_equals_ignore_case(topic, "FREE")) {
        terminal_writeline("FREE");
        terminal_writeline("Displays available NPTFS file slots.");
    } else if (string_equals_ignore_case(topic, "FSINFO")) {
        terminal_writeline("FSINFO");
        terminal_writeline("Displays NPTFS status and limits.");
    } else if (string_equals_ignore_case(topic, "FORMAT")) {
        terminal_writeline("FORMAT CONFIRM");
        terminal_writeline("Destructively formats the NPTFS data area.");
        terminal_writeline("WARNING: Existing NPTFS files will be erased.");
    } else if (string_equals_ignore_case(topic, "EXIT")) {
        terminal_writeline("EXIT");
        terminal_writeline("Explains why there is no host prompt to return to.");
    } else if (string_equals_ignore_case(topic, "DISKID")) {
        terminal_writeline("DISKID");
        terminal_writeline("Display primary-master ATA model and capacity");
    } else {
        terminal_write("No detailed help available for: ");
        terminal_writeline(topic);
    }
}

static void command_ver(void)
{
    terminal_writeline("NPT-DOS 1.01");
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

static void command_verbose(char *arguments)
{
    arguments = skip_spaces(arguments);

    if (*arguments == '\0') {
        verbose_mode = !verbose_mode;
    } else if (string_equals_ignore_case(arguments, "ON")) {
        verbose_mode = 1;
    } else if (string_equals_ignore_case(arguments, "OFF")) {
        verbose_mode = 0;
    } else {
        terminal_writeline("Usage: VERBOSE [ON|OFF]");
        return;
    }

    terminal_write("Verbose mode: ");
    terminal_writeline(verbose_mode ? "ON" : "OFF");
}

static void command_history(void)
{
    unsigned int oldest;
    unsigned int i;

    terminal_writeline("NPT-DOS Command History");
    terminal_writeline("-----------------------");

    if (history_count == 0) {
        terminal_writeline("No commands recorded.");
        return;
    }

    oldest = (history_next + HISTORY_CAPACITY - history_count)
             % HISTORY_CAPACITY;

    for (i = 0; i < history_count; i++) {
        unsigned int index = (oldest + i) % HISTORY_CAPACITY;

        print_unsigned(i + 1);
        terminal_write(": ");
        terminal_writeline(command_history_buffer[index]);
    }

    terminal_write("Stored commands: ");
    print_unsigned(history_count);
    terminal_writeline("");
}

static void command_prompt(char *arguments)
{
    size_t length = 0;
    size_t i;

    arguments = skip_spaces(arguments);

    if (*arguments == '\0') {
        terminal_write("Current prompt: ");
        terminal_write(shell_prompt);
        terminal_writeline(">");
        terminal_writeline("Usage: PROMPT <text>");
        return;
    }

    while (arguments[length] != '\0') {
        if (length >= PROMPT_MAX_LENGTH) {
            terminal_writeline("Error: Prompt cannot exceed 30 characters.");
            return;
        }

        if (arguments[length] < 32 || arguments[length] > 126) {
            terminal_writeline("Error: Prompt contains an invalid character.");
            return;
        }

        length++;
    }

    if (length == 0) {
        terminal_writeline("Error: Prompt cannot be empty.");
        return;
    }

    for (i = 0; i < length; i++) {
        shell_prompt[i] = arguments[i];
    }

    shell_prompt[length] = '\0';

    terminal_write("Prompt changed to: ");
    terminal_write(shell_prompt);
    terminal_writeline(">");
}

static void command_pause(void)
{
    terminal_write("Press any key to continue...");
    (void)keyboard_getchar();
    terminal_writeline("");
}

static void command_reboot(void)
{
    unsigned int attempts;

    terminal_writeline("Restarting NPT-DOS...");
    terminal_writeline("Unsaved shell history and prompt settings will be lost.");

    /*
     * Wait briefly for the keyboard controller input buffer
     * to become empty, then request a system reset.
     */
    for (attempts = 0; attempts < 1000000U; attempts++) {
        uint8_t status;

        __asm__ volatile (
            "inb %1, %0"
            : "=a"(status)
            : "Nd"((uint16_t)0x64)
        );

        if ((status & 0x02U) == 0) {
            break;
        }
    }

    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64)
    );

    /*
     * If the reset request did not reset the machine,
     * stop execution rather than returning to the shell.
     */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void record_command(const char *line)
{
    size_t i = 0;

    if (line == NULL || *line == '\0') return;

    while (line[i] != '\0' && i < INPUT_BUFFER_SIZE - 1) {
        command_history_buffer[history_next][i] = line[i];
        i++;
    }

    command_history_buffer[history_next][i] = '\0';

    history_next = (history_next + 1) % HISTORY_CAPACITY;

    if (history_count < HISTORY_CAPACITY) {
        history_count++;
    }
}

static void command_dir(char *arguments)
{
    int show_all = 0;
    int count = 0;
    int total = fs_file_count();
    int i;

    arguments = skip_spaces(arguments);

    if (*arguments != '\0') {
        if (string_equals_ignore_case(arguments, "/A")) {
            show_all = 1;
        } else {
            terminal_writeline("Usage: DIR [/A]");
            return;
        }
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    terminal_writeline("NPTFS Directory");
    terminal_writeline("----------------");

    for (i = 0; i < total; i++) {
        const char *name = fs_filename_at(i);

        if (name != NULL && (show_all || !file_is_hidden(name))) {
            int size = fs_file_size(name);

            terminal_write(name);
            print_file_attributes(name);
            terminal_write("  ");

            if (size >= 0) print_unsigned((unsigned int)size);
            else terminal_write("?");

            terminal_writeline(" bytes");
            count++;
        }
    }

    if (count == 0) terminal_writeline("No files found.");

    terminal_write("Visible files: ");
    print_unsigned((unsigned int)count);
    terminal_writeline("");
}

static void command_tree(char *arguments)
{
    int show_all = 0;
    int count = 0;
    int total = fs_file_count();
    int i;

    arguments = skip_spaces(arguments);

    if (*arguments != '\0') {
        if (string_equals_ignore_case(arguments, "/A")) {
            show_all = 1;
        } else {
            terminal_writeline("Usage: TREE [/A]");
            return;
        }
    }

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    terminal_writeline("NPTFS Directory Tree");
    terminal_writeline("NPTFS\\");

    for (i = 0; i < total; i++) {
        const char *name = fs_filename_at(i);

        if (name != NULL && (show_all || !file_is_hidden(name))) {
            terminal_write("|-- ");
            terminal_write(name);
            print_file_attributes(name);
            terminal_writeline("");
            count++;
        }
    }

    if (count == 0) terminal_writeline("|-- (empty)");

    terminal_write("");
    print_unsigned((unsigned int)count);
    terminal_writeline(" file(s)");
}

static void command_diskinfo(void)
{
    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    terminal_writeline("NPT-DOS Disk Information");
    terminal_writeline("------------------------");
    terminal_writeline("Filesystem: NPTFS v1");
    terminal_writeline("Sector size: 512 bytes");
    terminal_writeline("Directory sector: LBA 2");
    terminal_writeline("Data sectors: LBA 3-18");
    terminal_writeline("File slots: 16");
    terminal_writeline("Maximum file size: 255 bytes");
    terminal_writeline("Raw data area: 8192 bytes");
    terminal_writeline("Maximum usable file content: 4080 bytes");
    terminal_writeline("Note: This is NPTFS capacity, not total disk capacity.");
}

static void command_print_u32(uint32_t value)
{
    char buffer[11];
    int position = 10;

    buffer[position] = '\0';

    do {
        buffer[--position] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0 && position > 0);

    terminal_write(&buffer[position]);
}

static void command_diskid(void)
{
    ata_device_info_t info;
    uint32_t sectors;
    uint32_t mib_whole;
    uint32_t mib_fraction;
    uint32_t gib_whole;
    uint32_t gib_fraction;

    terminal_writeline("NPT-DOS DISK IDENTIFICATION");
    terminal_writeline("---------------------------");

    if (!ata_get_device_info(&info)) {
        terminal_writeline("No identified primary-master ATA device.");
        terminal_writeline("Check the VirtualBox storage controller.");
        return;
    }

    terminal_write("Device:              ");
    terminal_writeline("Primary Master");

    terminal_write("Model:               ");
    terminal_writeline(
        info.model[0] ? info.model : "(not reported)"
    );

    terminal_write("Reported LBA28:      ");

    if (info.lba28_sectors == 0) {
        terminal_writeline("Not reported");
    } else {
        command_print_u32(info.lba28_sectors);
        terminal_writeline(" sectors");
    }

    sectors = info.addressable_sectors;

    if (sectors == 0) {
        terminal_writeline("Addressable capacity: Unknown");
    } else {
        mib_whole = sectors / 2048U;
        mib_fraction = ((sectors % 2048U) * 10U) / 2048U;

        gib_whole = sectors / 2097152U;
        gib_fraction =
            ((sectors % 2097152U) * 10U) / 2097152U;

        terminal_write("Addressable capacity: ");
        command_print_u32(mib_whole);
        terminal_write(".");
        command_print_u32(mib_fraction);
        terminal_writeline(" MiB");

        terminal_write("Capacity in GiB:      ");
        command_print_u32(gib_whole);
        terminal_write(".");
        command_print_u32(gib_fraction);
        terminal_writeline(" GiB");
    }

    terminal_writeline("Addressing:           ATA LBA28");
    terminal_writeline("Sector size:          512 bytes");
    terminal_writeline("Disk writes:          None by this command");
}

static void command_free(void)
{
    int used;
    int free_slots;
    int raw_free_bytes;
    int content_free_bytes;

    if (!fs_is_ready()) {
        terminal_writeline("Error: No formatted NPTFS disk is ready.");
        return;
    }

    used = fs_file_count();
    free_slots = FS_MAX_FILES - used;
    raw_free_bytes = free_slots * 512;
    content_free_bytes = free_slots * FS_CONTENT_MAX;

    terminal_writeline("NPTFS Free Space");
    terminal_writeline("-----------------");

    terminal_write("File slots used: ");
    print_unsigned((unsigned int)used);
    terminal_writeline("");

    terminal_write("File slots free: ");
    print_unsigned((unsigned int)free_slots);
    terminal_writeline("");

    terminal_write("Maximum files: ");
    print_unsigned(FS_MAX_FILES);
    terminal_writeline("");

    terminal_write("Raw data-sector capacity free: ");
    print_unsigned((unsigned int)raw_free_bytes);
    terminal_writeline(" bytes");

    terminal_write("Additional file-content capacity: ");
    print_unsigned((unsigned int)content_free_bytes);
    terminal_writeline(" bytes");
}

static void command_fsinfo(void)
{
    terminal_writeline("NPTFS Filesystem Information");
    terminal_writeline("----------------------------");

    if (!fs_is_ready()) {
        terminal_writeline("Status: Not ready");
        terminal_writeline("Use FORMAT CONFIRM on the dedicated test disk.");
        return;
    }

    terminal_writeline("Status: Ready");
    terminal_writeline("Storage: IDE hard disk");
    terminal_writeline("Format: NPTFS v1");
    terminal_writeline("Maximum files: 16");
    terminal_writeline("Maximum filename: 15 characters");
    terminal_writeline("Maximum file contents: 255 bytes");
    terminal_writeline("Attributes: R, H, S");
}

static void command_copy(char *arguments)
{
    char *source = take_token(&arguments);
    char *destination = take_token(&arguments);
    const char *contents;
    fs_result_t result;

    arguments = skip_spaces(arguments);

    if (*source == '\0' || *destination == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: COPY <source> <destination>");
        return;
    }

    contents = fs_read(source);

    if (contents == NULL) {
        terminal_writeline("Error: Source file could not be read.");
        return;
    }

    /*
     * Copy the shared fs_read buffer before any further filesystem call.
     */
    {
        char copy_buffer[FS_CONTENT_MAX + 1];
        size_t i;
        int size = fs_file_size(source);

        if (size < 0) {
            terminal_writeline("Error: Source file could not be read.");
            return;
        }

        for (i = 0; i < (size_t)size; i++) copy_buffer[i] = contents[i];
        copy_buffer[size] = '\0';

        result = fs_create(destination);

        if (result != FS_OK) {
            report_fs_result(result);
            return;
        }

        result = fs_write(destination, copy_buffer);

        if (result != FS_OK) {
            (void)fs_delete(destination);
            report_fs_result(result);
            return;
        }

        result = fs_set_attributes(destination, fs_get_attributes(source));

        if (result != FS_OK) {
            (void)fs_delete(destination);
            report_fs_result(result);
            return;
        }
    }

    terminal_writeline("File copied successfully.");
}

static void command_rename(char *arguments)
{
    char *old_name = take_token(&arguments);
    char *new_name = take_token(&arguments);

    arguments = skip_spaces(arguments);

    if (*old_name == '\0' || *new_name == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: RENAME <old_name> <new_name>");
        return;
    }

    report_fs_result(fs_rename(old_name, new_name));
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

    for (i = 0; i < (size_t)existing_size; i++) combined[i] = contents[i];

    while (text[text_length] != '\0') text_length++;

    total_length = (size_t)existing_size;

    if (total_length > 0 && text_length > 0 &&
        combined[total_length - 1] != ' ' &&
        combined[total_length - 1] != '\t' &&
        text[0] != ' ' && text[0] != '\t') {
        add_separator = 1;
    }

    if (total_length + (size_t)add_separator + text_length > FS_CONTENT_MAX) {
        terminal_writeline("Error: Append would exceed 255 bytes.");
        return;
    }

    if (add_separator) combined[total_length++] = ' ';

    for (i = 0; i < text_length; i++) combined[total_length++] = text[i];

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
    if (length > HEAD_DISPLAY_LENGTH) length = HEAD_DISPLAY_LENGTH;

    for (i = 0; i < length; i++) output[i] = contents[i];
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

    arguments = skip_spaces(arguments);

    if (*name == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: CLEAR <name>");
        return;
    }

    if (fs_file_size(name) < 0) {
        terminal_writeline("Error: File not found.");
        return;
    }

    report_fs_result(fs_write(name, ""));
}

static void command_replace(char *arguments)
{
    char *name = take_token(&arguments);
    char *text = skip_spaces(arguments);

    if (*name == '\0' || *text == '\0') {
        terminal_writeline("Usage: REPLACE <name> <text>");
        return;
    }

    if (fs_file_size(name) < 0) {
        terminal_writeline("Error: File not found.");
        return;
    }

    report_fs_result(fs_write(name, text));
}

static void command_attrib(char *arguments)
{
    char *name = take_token(&arguments);
    char *operation = take_token(&arguments);
    int attributes;
    int flag;
    char sign;

    arguments = skip_spaces(arguments);

    if (*name == '\0' || *arguments != '\0') {
        terminal_writeline("Usage: ATTRIB <name> [+R|-R|+H|-H|+S|-S]");
        return;
    }

    attributes = fs_get_attributes(name);

    if (attributes < 0) {
        terminal_writeline("Error: File not found.");
        return;
    }

    if (*operation == '\0') {
        terminal_write("Attributes for ");
        terminal_write(name);
        terminal_write(": ");
        print_file_attributes(name);
        terminal_writeline("");
        return;
    }

    if (operation[0] != '+' && operation[0] != '-') {
        terminal_writeline("Usage: ATTRIB <name> [+R|-R|+H|-H|+S|-S]");
        return;
    }

    sign = operation[0];

    if (operation[2] != '\0') {
        terminal_writeline("Error: Specify one attribute at a time.");
        return;
    }

    switch (to_upper_char(operation[1])) {
        case 'R': flag = FS_ATTR_READ_ONLY; break;
        case 'H': flag = FS_ATTR_HIDDEN; break;
        case 'S': flag = FS_ATTR_SYSTEM; break;
        default:
            terminal_writeline("Error: Attribute must be R, H, or S.");
            return;
    }

    if (sign == '+') attributes |= flag;
    else attributes &= ~flag;

    report_fs_result(fs_set_attributes(name, attributes));
}


static void command_sysinfo(void)
{
    terminal_writeline("NPT-DOS System Information");
    terminal_writeline("--------------------------");
    terminal_writeline("Operating system: NPT-DOS");
    terminal_writeline("Version: 1.01");
    terminal_writeline("Publisher: Neptune Corporation");
    terminal_writeline("Architecture: 32-bit x86");
    terminal_writeline("Bootloader: GRUB / Multiboot");
    terminal_writeline("Console: VGA text mode");
    terminal_writeline("Keyboard: PS/2 polling");
    terminal_writeline("Timer: PIT channel 0, polled");
    terminal_writeline("Clock: CMOS real-time clock");
    terminal_writeline("Filesystem: NPTFS v1");
    terminal_writeline("Storage interface: ATA PIO");
    terminal_writeline("Interrupt-driven scheduling: Not implemented");
}

static void command_mem(void)
{
    terminal_writeline("NPT-DOS Memory Information");
    terminal_writeline("---------------------------");
    terminal_writeline("Architecture: 32-bit x86");
    terminal_writeline("Kernel stack reservation: 16384 bytes (16 KiB)");
    terminal_writeline("Stack: Single boot-time stack");
    terminal_writeline("Dynamic memory allocator: Not implemented");
    terminal_writeline("Physical memory map: Not yet available");
    terminal_writeline("Free physical memory: Unknown");
    terminal_writeline("");
    terminal_writeline("The stack reservation is not total RAM.");
}

static void command_uptime(void)
{
    uint32_t total_seconds = timer_get_uptime_seconds();
    unsigned int hours = total_seconds / 3600U;
    unsigned int minutes = (total_seconds / 60U) % 60U;
    unsigned int seconds = total_seconds % 60U;

    terminal_writeline("NPT-DOS Uptime");
    terminal_writeline("--------------");

    terminal_write("Elapsed: ");
    print_unsigned(hours);
    terminal_write(":");

    terminal_putchar((char)('0' + minutes / 10U));
    terminal_putchar((char)('0' + minutes % 10U));
    terminal_write(":");

    terminal_putchar((char)('0' + seconds / 10U));
    terminal_putchar((char)('0' + seconds % 10U));

    terminal_writeline(" (HH:MM:SS)");
    terminal_write("Timer ticks: ");
    print_unsigned(timer_get_ticks());
    terminal_writeline(" (approximately 100 ticks/second)");
}

static void print_two_digits(unsigned int value)
{
    terminal_putchar((char)('0' + (value / 10U) % 10U));
    terminal_putchar((char)('0' + value % 10U));
}

static void command_date(void)
{
    rtc_datetime_t dt;

    if (!rtc_read(&dt)) {
        terminal_writeline("Error: Could not read the real-time clock.");
        return;
    }

    terminal_write("Current date: ");
    print_unsigned(dt.year);
    terminal_write("-");
    print_two_digits(dt.month);
    terminal_write("-");
    print_two_digits(dt.day);
    terminal_writeline("");
}

static void command_time(void)
{
    rtc_datetime_t dt;

    if (!rtc_read(&dt)) {
        terminal_writeline("Error: Could not read the real-time clock.");
        return;
    }

    terminal_write("Current time: ");
    print_two_digits(dt.hour);
    terminal_write(":");
    print_two_digits(dt.minute);
    terminal_write(":");
    print_two_digits(dt.second);
    terminal_writeline(" (24-hour)");
}


static void execute_command(char *line)
{
    char *arguments = skip_spaces(line);
    char *command = take_token(&arguments);

    arguments = skip_spaces(arguments);

    if (*command == '\0') return;

    if (verbose_mode) {
        terminal_write("[VERBOSE] Executing: ");
         terminal_writeline(command);
    }

    if (string_equals_ignore_case(command, "HELP")) {
        command_help(arguments);
    } else if (string_equals_ignore_case(command, "VERBOSE")) {
        command_verbose(arguments);
    } else if (string_equals_ignore_case(command, "HISTORY")) {
        if (*arguments != '\0') {
            terminal_writeline("Usage: HISTORY");
        } else {
            command_history();
        }
    } else if (string_equals_ignore_case(command, "REBOOT")) {
        if (*arguments != '\0') {
            terminal_writeline("Usage: REBOOT");
        } else {
            command_reboot();
        }
    } else if (string_equals_ignore_case(command, "PROMPT")) {
        command_prompt(arguments);
    } else if (string_equals_ignore_case(command, "PAUSE")) {
        if (*arguments != '\0') {
            terminal_writeline("Usage: PAUSE");
        } else {
            command_pause();
        }
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
        command_dir(arguments);
    } else if (string_equals_ignore_case(command, "TREE")) {
        command_tree(arguments);
    } else if (string_equals_ignore_case(command, "DISKINFO")) {
        if (*arguments != '\0') terminal_writeline("Usage: DISKINFO");
        else command_diskinfo();
    } else if (string_equals_ignore_case(command, "FREE")) {
        if (*arguments != '\0') terminal_writeline("Usage: FREE");
        else command_free();
    } else if (string_equals_ignore_case(command, "FSINFO")) {
        command_fsinfo();
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

            if (contents == NULL) terminal_writeline("Error: File could not be read.");
            else terminal_writeline(contents);
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
    } else if (string_equals_ignore_case(command, "REN") ||
               string_equals_ignore_case(command, "RENAME")) {
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
    } else if (string_equals_ignore_case(command, "ATTRIB")) {
        command_attrib(arguments);
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

            if (fs_is_ready()) terminal_writeline("NPTFS format completed.");
            else terminal_writeline("NPTFS remains unavailable.");
        }
    } else if (string_equals_ignore_case(command, "SYSINFO")) {
        if (*arguments != '\0') terminal_writeline("Usage: SYSINFO");
        else command_sysinfo();
    } else if (string_equals_ignore_case(command, "MEM")) {
        if (*arguments != '\0') terminal_writeline("Usage: MEM");
        else command_mem();
    } else if (string_equals_ignore_case(command, "UPTIME")) {
        if (*arguments != '\0') terminal_writeline("Usage: UPTIME");
        else command_uptime();
    } else if (string_equals_ignore_case(command, "DATE")) {
        if (*arguments != '\0') terminal_writeline("Usage: DATE");
        else command_date();
    } else if (string_equals_ignore_case(command, "TIME")) {
        if (*arguments != '\0') terminal_writeline("Usage: TIME");
        else command_time();
    } else if (string_equals_ignore_case(command, "DISKID")) {
        if (*arguments != '\0') terminal_writeline("Usage: DISKID");
        else command_diskid();
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
    timer_initialize();
    keyboard_initialize();

    #ifndef NPT_INSTALLED_BUILD
        if (!setup_run()) {
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }
    #endif

    fs_initialize();

    terminal_writeline("NPT-DOS 1.01");
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

        terminal_write(shell_prompt);
        terminal_write("> ");

        for (;;) {
            char c = keyboard_getchar();

            if (c == '\n') {
                input[length] = '\0';
                terminal_writeline("");
                record_command(input);
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
