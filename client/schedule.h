#ifndef DASHBOARD_SCHEDULE_H
#define DASHBOARD_SCHEDULE_H
#include <time.h>
static int quiet_at_minutes(time_t now, int start, int end) {
    struct tm local;
    localtime_r(&now, &local);
    int minute = local.tm_hour * 60 + local.tm_min;
    if (start == end) return 0;
    if (start > end) return minute >= start || minute < end;
    return minute >= start && minute < end;
}
static time_t after_quiet_minutes(time_t candidate, int start, int end) {
    struct tm local;
    if (!quiet_at_minutes(candidate, start, end)) return candidate;
    localtime_r(&candidate, &local);
    if (start > end && local.tm_hour * 60 + local.tm_min >= start) local.tm_mday++;
    local.tm_hour = end / 60;
    local.tm_min = end % 60;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return mktime(&local);
}
#endif
