// platform/qemu-virt-sbi/platform_sbi.c

#include <stdint.h>
#include <stddef.h>
#include "riscv_csr.h"
#include "uart_16550.h"
#include "platform.h"
#include "sbi.h"

/* ========== 1. 输出相关 ========== */

// platform_sbi.c 里加一个小工具，用 platform_write 打 hex

void platform_put_hex64(uint64_t x)
{
  char buf[2 + 16 + 1];  // "0x" + 16 hex + '\0'
  int pos = 0;

  buf[pos++] = '0';
  buf[pos++] = 'x';

  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (x >> i) & 0xF;
    char c = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    buf[pos++] = c;
  }

  buf[pos] = '\0';
  platform_write(buf, (size_t)pos);
}


void platform_write(const char* buf, size_t len)
{
  if (!buf || len == 0)
    return;

  uart16550_write(buf, len);
}

void platform_putc(char c) {
  uart16550_putc(c);
}

void platform_puts(const char* s)
{
  if (!s)
    return;

  uart16550_puts(s);
}

void platform_idle(void)
{
  __asm__ volatile("nop");
  // __asm__ volatile("wfi");
}

/* ========== 2. 定时器相关 ========== */

platform_time_t platform_time_now(void)
{
  /* 依赖 OpenSBI 配好的 mcounteren，允许 S 模式读 time CSR */
  return csr_read(time);
}

void platform_timer_start_at(platform_time_t when)
{
  sbi_set_timer(when);
}

void platform_timer_start_after(platform_time_t delta_ticks)
{
  platform_time_t now = platform_time_now();
  platform_timer_start_at(now + delta_ticks);
}

/* ========== 3. 平台初始化 ========== */

void platform_early_init() {
  uart16550_init();
  // plic_init();
}
