// datetime.c
#include "datetime.h"

// 一天的秒数
#define SECS_PER_MIN   60
#define SECS_PER_HOUR  (60 * SECS_PER_MIN)
#define SECS_PER_DAY   (24 * SECS_PER_HOUR)

// month_days[0] 表示 1 月的天数，2 月先按平年来，闰年时会特殊处理
static const int month_days[12] = {
  31, // Jan
  28, // Feb (平年，闰年时当29)
  31, // Mar
  30, // Apr
  31, // May
  30, // Jun
  31, // Jul
  31, // Aug
  30, // Sep
  31, // Oct
  30, // Nov
  31  // Dec
};

static int is_leap_year(int year)
{
  // 公历规则：能被4整除且不能被100整除，或者能被400整除
  if ((year % 4) != 0)    return 0;
  if ((year % 100) != 0)  return 1;
  if ((year % 400) != 0)  return 0;
  return 1;
}

// 输入：epoch 秒（自 1970-01-01 00:00:00 UTC 起）
// 输出：UTC 时间
void epoch_to_utc_datetime(uint64_t epoch_sec, datetime_t *dt)
{
  // 0. 拆成“天数 + 当天秒数”
  uint64_t days = epoch_sec / SECS_PER_DAY;
  uint32_t sec_of_day = (uint32_t)(epoch_sec % SECS_PER_DAY);

  // 1. 计算时分秒（当天内部）
  dt->hour = (int)(sec_of_day / SECS_PER_HOUR);
  sec_of_day %= SECS_PER_HOUR;

  dt->min  = (int)(sec_of_day / SECS_PER_MIN);
  dt->sec  = (int)(sec_of_day % SECS_PER_MIN);

  // 2. 从 1970 年开始，一年一年减去天数，找到当前年份
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

  // 3. 在当年内部按月减去天数，找到当前月份
  int month = 0; // 0-based, 最后 +1
  while (month < 12) {
    int dim = month_days[month];
    if (month == 1 && is_leap_year(year)) {
      // 闰年 2 月
      dim = 29;
    }

    if (days >= (uint64_t)dim) {
      days -= (uint64_t)dim;
      month++;
    } else {
      break;
    }
  }

  dt->month = month + 1;        // 转为 1-12
  dt->day   = (int)days + 1;    // days 从 0 开始，+1 变成 1-31
}
