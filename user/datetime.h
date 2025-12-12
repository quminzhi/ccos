// datetime.h
#pragma once
#include <stdint.h>

typedef struct {
  int year;   // e.g. 2025
  int month;  // 1-12
  int day;    // 1-31
  int hour;   // 0-23
  int min;    // 0-59
  int sec;    // 0-59 (leap seconds ignored)
} datetime_t;

void epoch_to_utc_datetime(uint64_t epoch_sec, datetime_t *dt);
