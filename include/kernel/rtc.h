#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
} rtc_time_t;

int rtc_get_time(rtc_time_t* out);
u32 rtc_days_since_epoch(const rtc_time_t* time);
u32 rtc_seconds_since_midnight(const rtc_time_t* time);

#endif
