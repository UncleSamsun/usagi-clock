#include "timekeeper.h"

bool timekeeper_utc_plausible(int64_t utc_s)
{
    return utc_s >= TIME_VALID_MIN_UTC && utc_s < TIME_VALID_END_UTC;
}
