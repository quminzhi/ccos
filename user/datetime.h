// datetime.h
#pragma once
#include <stdint.h>

typedef struct {
  int year;   // e.g. 2025
  int month;  // 1-12
  int day;    // 1-31
  int hour;   // 0-23
  int min;    // 0-59
  int sec;    // 0-59 (忽略闰秒)
} datetime_t;

static int is_leap_year(int year)
{
  // 公历规则：能被4整除且不能被100整除，或者能被400整除
  if ((year % 4) != 0)    return 0;
  if ((year % 100) != 0)  return 1;
  if ((year % 400) != 0)  return 0;
  return 1;
}

void epoch_to_utc_datetime(uint64_t epoch_sec, datetime_t *dt);
