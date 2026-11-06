#include "rtc.h"
#include "io.h"

static int get_update_in_progress_flag(void) {
    outb(0x70, 0x8Au);
    io_wait();
    return (inb(0x71) & 0x80u);
}

static u8 get_rtc_register(u8 reg) {
    outb(0x70, (u8)(0x80u | (reg & 0x7Fu)));
    io_wait();
    return inb(0x71);
}

int rtc_get_time(rtc_time_t* out) {
    if (!out) return -1;
    rtc_time_t last;

    /* Read until two consecutive reads match */
    while (get_update_in_progress_flag()) { }
    out->second = get_rtc_register(0x00);
    out->minute = get_rtc_register(0x02);
    out->hour   = get_rtc_register(0x04);
    out->day    = get_rtc_register(0x07);
    out->month  = get_rtc_register(0x08);
    out->year   = get_rtc_register(0x09);

    for (int retry = 0; retry < 10; ++retry) {
        last = *out;
        while (get_update_in_progress_flag()) { }
        out->second = get_rtc_register(0x00);
        out->minute = get_rtc_register(0x02);
        out->hour   = get_rtc_register(0x04);
        out->day    = get_rtc_register(0x07);
        out->month  = get_rtc_register(0x08);
        out->year   = get_rtc_register(0x09);

        if (last.second == out->second && last.minute == out->minute &&
            last.hour == out->hour && last.day == out->day &&
            last.month == out->month && last.year == out->year) {
            break;
        }
    }

    u8 status_b = get_rtc_register(0x0B);

    /* Convert BCD to binary if not already binary */
    if (!(status_b & 0x04u)) {
        out->second = (u8)((out->second & 0x0Fu) + ((out->second >> 4) * 10u));
        out->minute = (u8)((out->minute & 0x0Fu) + ((out->minute >> 4) * 10u));
        out->hour   = (u8)(((out->hour & 0x0Fu) + (((out->hour & 0x70u) >> 4) * 10u)) | (out->hour & 0x80u));
        out->day    = (u8)((out->day & 0x0Fu) + ((out->day >> 4) * 10u));
        out->month  = (u8)((out->month & 0x0Fu) + ((out->month >> 4) * 10u));
        out->year   = (u16)((out->year & 0x0Fu) + ((out->year >> 4) * 10u));
    }

    /* Convert 12-hour clock to 24-hour clock if necessary */
    if (!(status_b & 0x02u) && (out->hour & 0x80u)) {
        out->hour = (u8)(((out->hour & 0x7Fu) + 12u) % 24u);
    }

    /* 4-digit Gregorian Year */
    out->year += 2000u;

    if (out->month < 1u || out->month > 12u || out->day < 1u || out->day > 31u ||
        out->hour > 23u || out->minute > 59u || out->second > 59u) {
        return -1;
    }
    return 0;
}

static int leap_year(u32 year) {
    return ((year % 4u) == 0u && (year % 100u) != 0u) || (year % 400u) == 0u;
}

u32 rtc_days_since_epoch(const rtc_time_t* time) {
    static const u16 month_days[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    u32 days = 0;
    u32 year;
    u8 month;
    if (!time || time->year < 1u) return 0;
    year = time->year;
    days = 365u * (year - 1u) + (year - 1u) / 4u - (year - 1u) / 100u + (year - 1u) / 400u;
    for (month = 1u; month < time->month; ++month) {
        days += month_days[month];
        if (month == 2u && leap_year(year)) ++days;
    }
    return days + (u32)time->day - 1u;
}

u32 rtc_seconds_since_midnight(const rtc_time_t* time) {
    if (!time) return 0;
    return (u32)time->hour * 3600u + (u32)time->minute * 60u + time->second;
}
