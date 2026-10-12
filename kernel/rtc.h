#ifndef NPT_DOS_RTC_H
#define NPT_DOS_RTC_H

typedef struct {
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
} rtc_datetime_t;

int rtc_read(rtc_datetime_t *datetime);

#endif
