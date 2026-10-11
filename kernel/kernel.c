#include <stdint.h>

#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

#define VGA_COLOR  0x0A

static void terminal_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = ((uint16_t)VGA_COLOR << 8) | ' ';
    }
}

static void terminal_write(const char *text, int row)
{
    int column = 0;

    while (*text && column < VGA_WIDTH) {
        VGA_MEMORY[row * VGA_WIDTH + column] =
            ((uint16_t)VGA_COLOR << 8) | (uint8_t)*text;

        text++;
        column++;
    }
}

void kernel_main(void)
{
    terminal_clear();

    terminal_write("NPT-DOS Build 002", 2);
    terminal_write("Neptune Corporation", 4);
    terminal_write("------------------------------", 5);
    terminal_write("Boot sequence successful.", 7);
    terminal_write("NPT-DOS kernel is running.", 9);
    terminal_write("System halted.", 11);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
