#include "ui_text.h"

#include <stdio.h>

#include "alarm_core.h"

void ui_fmt_clock(int64_t local_s, char *out, size_t n, bool *pm)
{
    alarm_civil_t c;
    alarm_civil_from_epoch(local_s, &c);

    int h12 = c.hour % 12;
    if (h12 == 0) {
        h12 = 12;   /* 0시와 12시는 "12" */
    }
    *pm = c.hour >= 12;
    snprintf(out, n, "%d:%02u", h12, c.minute);
}

void ui_fmt_date(int64_t local_s, char *out, size_t n)
{
    static const char *const WEEKDAYS[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
    static const char *const MONTHS[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    alarm_civil_t c;
    alarm_civil_from_epoch(local_s, &c);
    snprintf(out, n, "%s, %s %u", WEEKDAYS[c.weekday], MONTHS[c.month - 1], c.day);
}

void ui_fmt_countdown(int64_t seconds, char *out, size_t n)
{
    if (seconds < 0) {
        seconds = 0;
    }
    snprintf(out, n, "%d:%02d", (int)(seconds / 60), (int)(seconds % 60));
}
