#include <stdint.h>
#include "timer.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_INPUT_HZ 1193182U
#define PIT_DIVISOR 11932U

static volatile uint32_t timer_ticks;
static uint8_t last_output;
static int timer_ready;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

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

/*
 * Read the latched status of PIT channel 0.
 * The OUT signal is status bit 7.
 */
static uint8_t read_channel0_output(void)
{
    outb(PIT_COMMAND_PORT, 0xEC);
    return (uint8_t)((inb(PIT_CHANNEL0_PORT) >> 7) & 1U);
}

void timer_initialize(void)
{
    uint16_t divisor = (uint16_t)PIT_DIVISOR;

    timer_ticks = 0;
    timer_ready = 0;

    /*
     * Channel 0, access low/high byte, mode 3,
     * binary counter. No IRQs are enabled.
     */
    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor >> 8));

    last_output = read_channel0_output();
    timer_ready = 1;

    (void)PIT_INPUT_HZ;
}

void timer_poll(void)
{
    uint8_t output;

    if (!timer_ready) return;

    output = read_channel0_output();

    /*
     * Mode 3 produces a rising edge once per full
     * timer period, approximately 100 times/second.
     */
    if (output && !last_output) {
        timer_ticks++;
    }

    last_output = output;
}

uint32_t timer_get_ticks(void)
{
    timer_poll();
    return timer_ticks;
}

uint32_t timer_get_uptime_seconds(void)
{
    return timer_get_ticks() / 100U;
}
