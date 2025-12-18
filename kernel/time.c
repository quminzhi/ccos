/* kernel/time.c */
/* kernel/time.c */

#include <stdint.h>

#include "platform.h"
#include "log.h"

/* Real nanoseconds at boot; used to derive since-boot time */
static uint64_t boot_real_ns;

struct k_timespec {
  uint64_t tv_sec;
  uint32_t tv_nsec;
};

uint64_t
ktime_get_real_ns(void)
{
  return platform_rtc_read_ns();
}

void
ktime_get_real_ts(struct k_timespec *ts)
{
  uint64_t ns = ktime_get_real_ns();
  ts->tv_sec  = ns / 1000000000ull;
  ts->tv_nsec = (uint32_t)(ns % 1000000000ull);
}

uint64_t
ktime_get_monotonic_ns(void)
{
  uint64_t now = platform_rtc_read_ns();
  return now - boot_real_ns;
}

void
ktime_get_monotonic_ts(struct k_timespec *ts)
{
  uint64_t ns = ktime_get_monotonic_ns();
  ts->tv_sec  = ns / 1000000000ull;
  ts->tv_nsec = (uint32_t)(ns % 1000000000ull);
}

void
time_init(void)
{
  boot_real_ns = platform_rtc_read_ns();
  pr_info("time_init: boot_real_ns=%llu", (unsigned long long)boot_real_ns);
}
