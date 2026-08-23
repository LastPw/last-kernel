#ifndef LASTOS_FS_TIME_H
#define LASTOS_FS_TIME_H

#include "stdint.h"
#include "stddef.h"

typedef unsigned long time_t;

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long   tv_usec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

time_t time(time_t *tloc);
int clock_gettime(int clk_id, struct timespec *tp);
int gettimeofday(struct timeval *tv, void *tz);
struct tm *localtime_r(const time_t *timep, struct tm *result);
struct tm *gmtime_r(const time_t *timep, struct tm *result);

#endif /* LASTOS_FS_TIME_H */
