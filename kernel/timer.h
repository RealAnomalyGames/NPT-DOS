#ifndef NPT_DOS_TIMER_H
#define NPT_DOS_TIMER_H

#include <stdint.h>

void timer_initialize(void);
void timer_poll(void);
uint32_t timer_get_ticks(void);
uint32_t timer_get_uptime_seconds(void);

#endif
