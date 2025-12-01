// platform/qemu-virt-sbi/platform_sbi.c

#include <stdint.h>
#include <stddef.h>
#include "riscv_csr.h"
#include "uart_16550.h"
#include "platform.h"
#include "sbi.h"

extern void trap_entry(void); /* arch/riscv/trap.S 里定义 */

static platform_timer_handler_t g_timer_handler = 0;

/* ========== 1. 输出相关 ========== */

// platform_sbi.c 里加一个小工具，用 platform_write 打 hex

static void platform_put_hex64(uint64_t x)
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

void platform_puts(const char* s)
{
  if (!s)
    return;

  uart16550_puts(s);
}

void platform_idle(void)
{
  __asm__ volatile("wfi");
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

void platform_init(platform_timer_handler_t timer_handler)
{
  uart16550_init();

  g_timer_handler = timer_handler;

  /* 1. stvec 指向 S 模式 trap 入口（汇编包装） */
  csr_write(stvec, (reg_t)trap_entry);

  platform_puts("platform_init: stvec = 0x");
  platform_put_hex64((uint64_t)trap_entry);
  platform_puts("\n");

  /* 2. 打开 S 模式全局中断 (sstatus.SIE) */
  reg_t sstatus = csr_read(sstatus);
  sstatus |= SSTATUS_SIE;          /* 或 MSTATUS_SIE，bit 位置相同 */
  csr_write(sstatus, sstatus);

  /* 3. 只开 S 模式定时器中断 (sie.STIE) */
  reg_t sie = csr_read(sie);
  sie |= SIE_STIE;                 /* Supervisor Timer Interrupt Enable */
  csr_write(sie, sie);
}

/* ========== 4. 平台层暴露的 timer 中断处理入口 ========== */
/* 由内核 trap handler (kernel/trap.c) 在检测到 S 模式定时器中断时调用 */

void platform_handle_timer_interrupt(void)
{
  if (g_timer_handler) {
    g_timer_handler();
  } else {
    platform_puts("timer interrupt, but no handler registered!\n");
  }
}
