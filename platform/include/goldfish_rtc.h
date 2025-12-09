// goldfish_rtc.h
#pragma once
#include <stdint.h>

void goldfish_rtc_init(void);

// 读 RTC 的 64bit 时间戳 (ns)
uint64_t goldfish_rtc_read_ns(void);

// 设置在 delay_ns 之后触发一次中断 (absolute alarm)
void goldfish_rtc_set_alarm_after(uint64_t delay_ns);

uint32_t goldfish_rtc_get_irq(void);
void goldfish_rtc_irq_handler(uint32_t irq, void *arg);
