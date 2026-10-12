#include <stdint.h>
#include "keyboard.h"
#include "timer.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

static int shift_pressed;
static int caps_lock;

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

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
    [0x35] = '/', [0x37] = '*', [0x39] = ' ',
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4A] = '-', [0x4B] = '4', [0x4C] = '5',
    [0x4D] = '6', [0x4E] = '+', [0x4F] = '1',
    [0x50] = '2', [0x51] = '3', [0x52] = '0',
    [0x53] = '.'
};

static char shifted_character(char c)
{
    switch (c) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case ';': return ':';
        case '\'': return '"';
        case '`': return '~';
        case '\\': return '|';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        default: return c;
    }
}

void keyboard_initialize(void)
{
    shift_pressed = 0;
    caps_lock = 0;

    /*
     * Enable the first PS/2 keyboard port.
     * A short bounded wait avoids hanging forever if the
     * controller is not present or is not responding.
     */
    for (unsigned int i = 0; i < 100000; i++) {
        if (!(inb(KEYBOARD_STATUS_PORT) & 0x02)) {
            break;
        }
    }

    outb(0x64, 0xAE);
}

char keyboard_getchar(void)
{
    for (;;) {
        timer_poll();

        if (!(inb(KEYBOARD_STATUS_PORT) & 0x01)) {
            continue;
        }

        uint8_t scancode = inb(KEYBOARD_DATA_PORT);

        /* Ignore extended-key sequences in this build. */
        if (scancode == 0xE0 || scancode == 0xE1) {
            continue;
        }

        /* Key release: high bit set. */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;

            if (released == 0x2A || released == 0x36) {
                shift_pressed = 0;
            }

            continue;
        }

        /* Left or right Shift pressed. */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        }

        if (scancode == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }

        char c = keymap[scancode & 0x7F];

        if (c == '\0' || c == '\t') {
            continue;
        }

        if (c >= 'a' && c <= 'z') {
            if (shift_pressed != caps_lock) {
                c = (char)(c - 'a' + 'A');
            }
        } else if (shift_pressed) {
            c = shifted_character(c);
        }

        return c;
    }
}
