// platform/qemu-virt-sbi/platform_sbi.c

#include <stdint.h>
#include <stddef.h>
#include "riscv_csr.h"
#include "uart_16550.h"
#include "goldfish_rtc.h"
#include "platform.h"
#include "plic.h"
#include "sbi.h"

static const void* g_dtb;  // 全局 DTB 指针

const void* platform_get_dtb(void) { return g_dtb; }

void platform_set_dtb(uintptr_t dtb_pa) { g_dtb = (const void*)dtb_pa; }

/* ========== 输出相关 ========== */

void platform_uart_init() { uart16550_init(); }

void platform_put_dec_us(uint64_t x);

void platform_put_dec_s(int64_t v)
{
  if (v < 0) {
    // 先输出负号
    platform_write("-", 1);

    // 处理负数的绝对值，注意 INT64_MIN 的情况不能直接 -v
    uint64_t mag = (uint64_t)(-(v + 1)) + 1;
    platform_put_dec_us(mag);
  } else {
    platform_put_dec_us((uint64_t)v);
  }
}

void platform_put_dec_us(uint64_t x)
{
  // uint64_t 最大是 18446744073709551615，一共 20 位十进制
  char buf[20 + 1];  // 20 digits + '\0'
  int pos = 0;

  if (x == 0) {
    buf[pos++] = '0';
  } else {
    char tmp[20];
    int len = 0;

    // 先把数字倒着存到 tmp 里
    while (x > 0) {
      uint64_t q = x / 10;
      uint32_t r = (uint32_t)(x - q * 10);  // 等价于 x % 10，但少一次除法
      tmp[len++] = (char)('0' + r);
      x          = q;
    }

    // 再倒过来写入 buf
    for (int i = len - 1; i >= 0; --i) {
      buf[pos++] = tmp[i];
    }
  }

  buf[pos] = '\0';
  platform_write(buf, (size_t)pos);
}

void platform_put_hex64(uint64_t x)
{
  char buf[2 + 16 + 1];  // "0x" + 16 hex + '\0'
  int pos    = 0;

  buf[pos++] = '0';
  buf[pos++] = 'x';

  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (x >> i) & 0xF;
    char c      = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    buf[pos++]  = c;
  }

  buf[pos] = '\0';
  platform_write(buf, (size_t)pos);
}

void platform_write(const char* buf, size_t len)
{
  if (!buf || len == 0) return;

  uart16550_write(buf, len);
}

void platform_putc(char c) { uart16550_putc(c); }

void platform_puts(const char* s)
{
  if (!s) return;

  uart16550_puts(s);
}

/* ========== 定时器相关 ========== */

platform_time_t platform_time_now(void)
{
  /* 依赖 OpenSBI 配好的 mcounteren，允许 S 模式读 time CSR */
  return csr_read(time);
}

void platform_timer_start_at(platform_time_t when) { sbi_set_timer(when); }

void platform_timer_start_after(platform_time_t delta_ticks)
{
  platform_time_t now = platform_time_now();
  platform_timer_start_at(now + delta_ticks);
}

/* ========== RTC ========== */

void platform_rtc_init(void) { goldfish_rtc_init(); }

uint64_t platform_rtc_read_ns(void) { return goldfish_rtc_read_ns(); }

void platform_rtc_set_alarm_after(uint64_t delay_ns)
{
  goldfish_rtc_set_alarm_after(delay_ns);
}

/* ========== PLIC & IRQ ========== */

void platform_plic_init(void)
{
  // 1. S-mode PLIC context
  plic_init_s_mode();

  // 2. 开 UART0 RTC 中断
  uint32_t uart_irq = uart16550_get_irq();
  plic_set_priority(uart_irq, 1);
  plic_enable_irq(uart_irq);

  uint32_t rtc_irq = goldfish_rtc_get_irq();
  plic_set_priority(rtc_irq, 1);
  plic_enable_irq(rtc_irq);

  // 3. S-mode 打开外部中断
  csr_set(sie, SIE_SEIE);
  csr_set(sstatus, SSTATUS_SIE);
}

/* ========== IRQ handler 注册表 ========== */

#define MAX_IRQ 64  // QEMU virt 上基本够用，后面不够再扩
typedef void (*irq_handler_t)(void);
static irq_handler_t s_irq_table[MAX_IRQ];

void platform_register_irq_handler(uint32_t irq, irq_handler_t handler)
{
  if (irq < MAX_IRQ) {
    s_irq_table[irq] = handler;
  }
}

void platform_handle_s_external(struct trapframe* tf)
{
  (void)tf;  // 暂时用不到

  uint32_t irq = plic_claim();
  if (!irq) {
    return;  // spurious
  }

  irq_handler_t h = NULL;
  if (irq < MAX_IRQ) {
    h = s_irq_table[irq];
  }

  if (h) {
    h();
  } else {
    platform_puts("unknown PLIC irq\n");
  }

  plic_complete(irq);
}

/* ========== MISC ========== */

void platform_idle(void)
{
  __asm__ volatile("nop");
  // __asm__ volatile("wfi");
}
