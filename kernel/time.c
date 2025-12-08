// kernel/time.c
#include <stdint.h>
#include "platform.h"

// boot 时刻的 “real ns”，用于构造 since-boot
static uint64_t boot_real_ns;

struct k_timespec {
  uint64_t tv_sec;
  uint32_t tv_nsec;
};

uint64_t ktime_get_real_ns(void) { return platform_rtc_read_ns(); }

void ktime_get_real_ts(struct k_timespec *ts)
{
  uint64_t ns = ktime_get_real_ns();
  ts->tv_sec  = ns / 1000000000ull;
  ts->tv_nsec = (uint32_t)(ns % 1000000000ull);
}

uint64_t ktime_get_monotonic_ns(void)
{
  uint64_t now = platform_rtc_read_ns();
  return now - boot_real_ns;
}

void ktime_get_monotonic_ts(struct k_timespec *ts)
{
  uint64_t ns = ktime_get_monotonic_ns();
  ts->tv_sec  = ns / 1000000000ull;
  ts->tv_nsec = (uint32_t)(ns % 1000000000ull);
}

void time_init(void)
{
  // 目前啥也不用干，将来如果你要：
  // - 在这里记录 boot_mono/boot_real
  // - 或者校准 RTC offset
  boot_real_ns = platform_rtc_read_ns();
}
