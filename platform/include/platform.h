// platform.h
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

struct trapframe;

/* 用 time CSR 的 tick 作为时间单位（QEMU virt 上通常是 10MHz） */
typedef uint64_t platform_time_t;

/* 输出字符串（当前实现：直接用 QEMU virt 的 UART0 MMIO） */
void platform_uart_init();
void platform_putc(char c);
void platform_puts(const char *s);
void platform_write(const char *buf, size_t len);
void platform_put_hex64(uint64_t x);
void platform_put_dec_s(int64_t v);
void platform_put_dec_us(uint64_t x);

/* 让 CPU idle，一般就是 wfi */
void platform_idle(void);

/* 当前时间：读取 time CSR */
platform_time_t platform_time_now(void);

/* 安排下一次定时器中断：当前时间 + delta_ticks */
void platform_timer_start_after(platform_time_t delta_ticks);

/* 直接按绝对时间设置定时器（stime_value = time CSR 的刻度） */
void platform_timer_start_at(platform_time_t when);

void platform_plic_init(void);
void platform_handle_s_external(struct trapframe *tf);

#endif /* PLATFORM_H */
