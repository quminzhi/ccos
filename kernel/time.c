// kernel/time.c
#include <stdint.h>
#include "platform.h"

// 以后你可以再加 monotonic，这里先只做 REALTIME

uint64_t ktime_get_real_ns(void) { return platform_rtc_read_ns(); }

struct k_timespec {
  uint64_t tv_sec;
  uint32_t tv_nsec;
};

// 内核内部用的 timespec（你也可以直接用 POSIX 的 timespec）
void ktime_get_real_ts(struct k_timespec *ts)
{
  uint64_t ns = ktime_get_real_ns();
  ts->tv_sec  = ns / 1000000000ull;
  ts->tv_nsec = (uint32_t)(ns % 1000000000ull);
}

void time_init(void)
{
  // 目前啥也不用干，将来如果你要：
  // - 在这里记录 boot_mono/boot_real
  // - 或者校准 RTC offset
  // 再往里加
}
