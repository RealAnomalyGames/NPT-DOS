#include <stddef.h>
#include <stdint.h>

#include "terminal.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

#define TERMINAL_COLOR 0x07

static size_t terminal_row;
static size_t terminal_column;

static uint16_t make_vga_entry(char character)
{
    return ((uint16_t)TERMINAL_COLOR << 8) |
           (uint8_t)character;
}

static void terminal_put_entry_at(
    char character,
    size_t column,
    size_t row
)
{
    VGA_MEMORY[row * VGA_WIDTH + column] =
        make_vga_entry(character);
}

static void terminal_scroll(void)
{
    for (size_t row = 1; row < VGA_HEIGHT; row++) {
        for (size_t column = 0; column < VGA_WIDTH; column++) {
            VGA_MEMORY[(row - 1) * VGA_WIDTH + column] =
                VGA_MEMORY[row * VGA_WIDTH + column];
        }
    }

    for (size_t column = 0; column < VGA_WIDTH; column++) {
        terminal_put_entry_at(' ', column, VGA_HEIGHT - 1);
    }

    terminal_row = VGA_HEIGHT - 1;
}

void terminal_clear(void)
{
    for (size_t row = 0; row < VGA_HEIGHT; row++) {
        for (size_t column = 0; column < VGA_WIDTH; column++) {
            terminal_put_entry_at(' ', column, row);
        }
    }

    terminal_row = 0;
    terminal_column = 0;
}

void terminal_initialize(void)
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_clear();
}

void terminal_putchar(char character)
{
    if (character == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else if (character == '\r') {
        terminal_column = 0;
    } else if (character == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            terminal_put_entry_at(
                ' ',
                terminal_column,
                terminal_row
            );
        }
    } else if (character == '\t') {
        size_t spaces = 4 - (terminal_column % 4);

        while (spaces--) {
            terminal_putchar(' ');
        }

        return;
    } else {
        terminal_put_entry_at(
            character,
            terminal_column,
            terminal_row
        );

        terminal_column++;

        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }

    if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
    }
}

void terminal_write(const char *text)
{
    while (*text) {
        terminal_putchar(*text++);
    }
}

void terminal_writeline(const char *text)
{
    terminal_write(text);
    terminal_putchar('\n');
}
