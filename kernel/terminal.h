#ifndef NPT_DOS_TERMINAL_H
#define NPT_DOS_TERMINAL_H

void terminal_initialize(void);
void terminal_clear(void);
void terminal_putchar(char character);
void terminal_write(const char *text);
void terminal_writeline(const char *text);
void terminal_backspace(void);

#endif
