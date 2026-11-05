#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "schedule.h"
static time_t at(int year,int month,int day,int hour,int minute) {
    struct tm t={0};t.tm_year=year-1900;t.tm_mon=month-1;t.tm_mday=day;
    t.tm_hour=hour;t.tm_min=minute;t.tm_isdst=-1;return mktime(&t);
}
int main(void) {
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
    assert(!quiet_at_minutes(at(2026,1,1,21,59),1320,420));
    assert(quiet_at_minutes(at(2026,1,1,22,0),1320,420));
    assert(quiet_at_minutes(at(2026,1,2,6,59),1320,420));
    assert(!quiet_at_minutes(at(2026,1,2,7,0),1320,420));
    assert(after_quiet_minutes(at(2026,12,31,23,0),1320,420)==at(2027,1,1,7,0));
    assert(!quiet_at_minutes(at(2026,1,1,22,29),1350,435));
    assert(quiet_at_minutes(at(2026,1,1,22,30),1350,435));
    assert(after_quiet_minutes(at(2026,1,2,7,14),1350,435)==at(2026,1,2,7,15));
    assert(!quiet_at_minutes(at(2026,1,2,7,15),1350,435));
    assert(!quiet_at_minutes(at(2026,1,1,12,0),600,600));
    assert(quiet_at_minutes(at(2026,1,1,12,0),600,900));
    assert(after_quiet_minutes(at(2026,1,1,12,0),600,900)==at(2026,1,1,15,0));
    time_t spring=at(2026,3,28,22,0),autumn=at(2026,10,24,22,0);
    assert(after_quiet_minutes(spring,1320,420)-spring==8*3600);
    assert(after_quiet_minutes(autumn,1320,420)-autumn==10*3600);
    puts("Schedule tests passed (boundaries, minutes, year rollover, DST).");return 0;
}
