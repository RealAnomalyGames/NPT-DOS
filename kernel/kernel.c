
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

#define VGA_GREY 0x07

static size_t terminal_row = 0;
static size_t terminal_column = 0;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void update_cursor(void)
{
    uint16_t position =
        (uint16_t)(terminal_row * VGA_WIDTH + terminal_column);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(position >> 8));
}

static void terminal_clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = ((uint16_t)VGA_GREY << 8) | ' ';
    }

    terminal_row = 0;
    terminal_column = 0;
    update_cursor();
}

static void terminal_scroll(void)
{
    for (size_t row = 1; row < VGA_HEIGHT; row++) {
        for (size_t col = 0; col < VGA_WIDTH; col++) {
            VGA_MEMORY[(row - 1) * VGA_WIDTH + col] =
                VGA_MEMORY[row * VGA_WIDTH + col];
        }
    }

    for (size_t col = 0; col < VGA_WIDTH; col++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
            ((uint16_t)VGA_GREY << 8) | ' ';
    }

    terminal_row = VGA_HEIGHT - 1;
}

static void terminal_putchar(char c)
{
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\r') {
        terminal_column = 0;
    } else if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
                ((uint16_t)VGA_GREY << 8) | ' ';
        }
    } else if (c >= ' ') {
        VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_column] =
            ((uint16_t)VGA_GREY << 8) | (uint8_t)c;

        terminal_column++;

        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }

    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }

    update_cursor();
}

static void terminal_write(const char *text)
{
    while (*text) {
        terminal_putchar(*text++);
    }
}

static int string_equal(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;

        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';

        if (ca != cb) return 0;
    }

    return *a == '\0' && *b == '\0';
}

/* PS/2 keyboard, Set 1 scancode translation. */
static const char keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3',
    [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o',
    [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.',
    [0x35] = '/', [0x39] = ' '
};

static const char shifted_keymap[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#',
    [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&', [0x09] = '*', [0x0A] = '(',
    [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O',
    [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
    [0x32] = 'M', [0x33] = '<', [0x34] = '>',
    [0x35] = '?', [0x39] = ' '
};

static int shift_pressed = 0;

static char keyboard_read_char(void)
{
    for (;;) {
        if (!(inb(0x64) & 1)) {
            continue;
        }

        uint8_t scancode = inb(0x60);

        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        }

        if (scancode == 0xAA || scancode == 0xB6) {
            shift_pressed = 0;
            continue;
        }

        /* Ignore key releases and extended-key prefixes. */
        if (scancode & 0x80) continue;
        if (scancode == 0xE0 || scancode == 0xE1) continue;

        if (scancode >= 128) continue;

        char c = shift_pressed
            ? shifted_keymap[scancode]
            : keymap[scancode];

        if (c != '\0' && c != '\t') return c;
    }
}

static void shell_help(void)
{
    terminal_write("Available commands:\n");
    terminal_write("  HELP       Show commands\n");
    terminal_write("  VER        Show system version\n");
    terminal_write("  CLS        Clear the screen\n");
    terminal_write("  ECHO text  Display a message\n");
    terminal_write("  ABOUT      About NPT-DOS\n");
}

static void shell_execute(char *command)
{
    while (*command == ' ') command++;

    if (*command == '\0') return;

    if (string_equal(command, "HELP")) {
        shell_help();
    } else if (string_equal(command, "VER")) {
        terminal_write("NPT-DOS Build 003\n");
    } else if (string_equal(command, "CLS")) {
        terminal_clear();
    } else if (string_equal(command, "ABOUT")) {
        terminal_write("NPT-DOS - Neptune Corporation\n");
        terminal_write("A command-line operating system.\n");
    } else if (
        (command[0] == 'E' || command[0] == 'e') &&
        (command[1] == 'C' || command[1] == 'c') &&
        (command[2] == 'H' || command[2] == 'h') &&
        (command[3] == 'O' || command[3] == 'o') &&
        (command[4] == ' ' || command[4] == '\0')
    ) {
        char *message = command + 4;
        while (*message == ' ') message++;
        terminal_write(message);
        terminal_putchar('\n');
    } else {
        terminal_write("Bad command or file name.\n");
        terminal_write("Type HELP for a list of commands.\n");
    }
}

void kernel_main(void)
{
    terminal_clear();

    terminal_write("NPT-DOS Build 003\n");
    terminal_write("Neptune Corporation\n");
    terminal_write("Terminal initialized.\n\n");
    terminal_write("Type HELP to view available commands.\n\n");

    char command[128];
    size_t length;

    for (;;) {
        terminal_write("NPT-DOS> ");
        length = 0;

        for (;;) {
            char c = keyboard_read_char();

            if (c == '\n') {
                terminal_putchar('\n');
                command[length] = '\0';
                break;
            }

            if (c == '\b') {
                if (length > 0) {
                    length--;
                    terminal_putchar('\b');
                }
                continue;
            }

            if (length < sizeof(command) - 1) {
                command[length++] = c;
                terminal_putchar(c);
            }
        }

        shell_execute(command);
    }
}
