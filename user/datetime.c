/* datetime.c */

#include "datetime.h"

/* Seconds per unit. */
#define SECS_PER_MIN   60
#define SECS_PER_HOUR  (60 * SECS_PER_MIN)
#define SECS_PER_DAY   (24 * SECS_PER_HOUR)

/* month_days[0] is January; February uses leap-year logic below. */
static const int month_days[12] = {
  31, /* Jan */
  28, /* Feb (29 in leap years) */
  31, /* Mar */
  30, /* Apr */
  31, /* May */
  30, /* Jun */
  31, /* Jul */
  31, /* Aug */
  30, /* Sep */
  31, /* Oct */
  30, /* Nov */
  31  /* Dec */
};

static int is_leap_year(int year)
{
  /* Gregorian rule: divisible by 4 and not by 100, unless divisible by 400. */
  if ((year % 4) != 0)    return 0;
  if ((year % 100) != 0)  return 1;
  if ((year % 400) != 0)  return 0;
  return 1;
}

/* Input: epoch seconds since 1970-01-01 00:00:00 UTC. */
/* Output: UTC time components. */
void epoch_to_utc_datetime(uint64_t epoch_sec, datetime_t *dt)
{
  /* Step 0: split into days + intra-day seconds. */
  uint64_t days = epoch_sec / SECS_PER_DAY;
  uint32_t sec_of_day = (uint32_t)(epoch_sec % SECS_PER_DAY);

  /* Step 1: compute hour/min/sec within the day. */
  dt->hour = (int)(sec_of_day / SECS_PER_HOUR);
  sec_of_day %= SECS_PER_HOUR;

  dt->min  = (int)(sec_of_day / SECS_PER_MIN);
  dt->sec  = (int)(sec_of_day % SECS_PER_MIN);

  /* Step 2: subtract years starting from 1970 to find the current year. */
  int year = 1970;
  while (1) {
    int days_in_year = is_leap_year(year) ? 366 : 365;
    if (days >= (uint64_t)days_in_year) {
      days -= (uint64_t)days_in_year;
      year++;
    } else {
      break;
    }
  }
  dt->year = year;

  /* Step 3: subtract months within the current year. */
  int month = 0; /* 0-based; add 1 later. */
  while (month < 12) {
    int dim = month_days[month];
    if (month == 1 && is_leap_year(year)) {
      /* Leap-year February. */
      dim = 29;
    }

    if (days >= (uint64_t)dim) {
      days -= (uint64_t)dim;
      month++;
    } else {
      break;
    }
  }

  dt->month = month + 1;        /* Convert to 1-12. */
  dt->day   = (int)days + 1;    /* days was zero-based; convert to 1-31. */
}
