// kernel/time.h
#pragma once
#include <stdint.h>

struct k_timespec {
  uint64_t tv_sec;
  uint32_t tv_nsec;
};

void time_init(void);
uint64_t ktime_get_real_ns(void);
void ktime_get_real_ts(struct k_timespec *ts);
uint64_t ktime_get_monotonic_ns(void);
void ktime_get_monotonic_ts(struct k_timespec *ts);
