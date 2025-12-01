// platform.h
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/* 用 time CSR 的 tick 作为时间单位（QEMU virt 上通常是 10MHz） */
typedef uint64_t platform_time_t;

/* 定时器中断回调类型：在 S 模式 trap 上下文里被调用 */
typedef void (*platform_timer_handler_t)(void);

/*
 * 平台初始化：
 *  - 设置 stvec = trap_entry（S 模式 trap 入口）
 *  - 打开 S 模式中断（SSTATUS.SIE / SIE.STIE）
 *  - 保存一个定时器回调指针，后面 timer interrupt 会调用它
 */
void platform_init(platform_timer_handler_t timer_handler);

/* 输出字符串（当前实现：直接用 QEMU virt 的 UART0 MMIO） */
void platform_puts(const char *s);
void platform_write(const char *buf, size_t len);

/* 让 CPU idle，一般就是 wfi */
void platform_idle(void);

/* 当前时间：读取 time CSR */
platform_time_t platform_time_now(void);

/* 安排下一次定时器中断：当前时间 + delta_ticks */
void platform_timer_start_after(platform_time_t delta_ticks);

/* 直接按绝对时间设置定时器（stime_value = time CSR 的刻度） */
void platform_timer_start_at(platform_time_t when);

/* S 模式定时器中断发生时，平台层的处理入口（会转回内核注册的回调） */
void platform_handle_timer_interrupt(void);

#endif /* PLATFORM_H */
