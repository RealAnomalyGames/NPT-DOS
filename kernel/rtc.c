#include <stdint.h>
#include "rtc.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT  0x71

#define CMOS_SECONDS    0x00
#define CMOS_MINUTES    0x02
#define CMOS_HOURS      0x04
#define CMOS_DAY        0x07
#define CMOS_MONTH      0x08
#define CMOS_YEAR       0x09
#define CMOS_STATUS_A   0x0A
#define CMOS_STATUS_B   0x0B

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

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX_PORT, reg);
    return inb(CMOS_DATA_PORT);
}

static int wait_for_update(void)
{
    unsigned int attempts;

    for (attempts = 0; attempts < 1000000U; attempts++) {
        if ((cmos_read(CMOS_STATUS_A) & 0x80U) == 0) {
            return 1;
        }
    }

    return 0;
}

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static int read_snapshot(rtc_datetime_t *dt,
                         uint8_t *status_b,
                         uint8_t *raw_hour)
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;

    if (!wait_for_update()) return 0;

    *status_b = cmos_read(CMOS_STATUS_B);

    seconds = cmos_read(CMOS_SECONDS);
    minutes = cmos_read(CMOS_MINUTES);
    hours = cmos_read(CMOS_HOURS);
    day = cmos_read(CMOS_DAY);
    month = cmos_read(CMOS_MONTH);
    year = cmos_read(CMOS_YEAR);

    if (!wait_for_update()) return 0;

    /*
     * Retry if the seconds field changed during the read.
     * The caller performs one additional stable snapshot.
     */
    if (seconds != cmos_read(CMOS_SECONDS)) return 0;

    *raw_hour = hours;

    if ((*status_b & 0x04U) == 0) {
        seconds = from_bcd(seconds);
        minutes = from_bcd(minutes);
        hours = (uint8_t)(hours & 0x7FU);
        hours = from_bcd(hours);
        day = from_bcd(day);
        month = from_bcd(month);
        year = from_bcd(year);
    } else {
        hours = (uint8_t)(hours & 0x7FU);
    }

    if ((*status_b & 0x02U) == 0) {
        int is_pm = (*raw_hour & 0x80U) != 0;

        if (hours == 12) hours = 0;
        if (is_pm) hours = (uint8_t)(hours + 12);
    }

    dt->year = 2000U + year;
    dt->month = month;
    dt->day = day;
    dt->hour = hours;
    dt->minute = minutes;
    dt->second = seconds;

    return 1;
}

int rtc_read(rtc_datetime_t *datetime)
{
    rtc_datetime_t first;
    rtc_datetime_t second;
    uint8_t status_b;
    uint8_t raw_hour;
    unsigned int attempts;

    if (datetime == 0) return 0;

    for (attempts = 0; attempts < 5; attempts++) {
        if (!read_snapshot(&first, &status_b, &raw_hour)) continue;
        if (!read_snapshot(&second, &status_b, &raw_hour)) continue;

        if (first.second == second.second &&
            first.minute == second.minute &&
            first.hour == second.hour &&
            first.day == second.day &&
            first.month == second.month &&
            first.year == second.year) {
            *datetime = second;
            return 1;
        }
    }

    return 0;
}
