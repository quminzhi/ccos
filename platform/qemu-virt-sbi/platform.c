// platform/qemu-virt-sbi/platform.c

#include <stdint.h>
#include <stddef.h>
#include "uart_16550.h"
#include "goldfish_rtc.h"
#include "platform.h"
#include "plic.h"
#include "timer.h"

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

void platform_timer_init(uintptr_t hartid) { timer_init(hartid); }

platform_time_t platform_time_now(void) { return timer_now(); }

void platform_timer_start_at(platform_time_t when) { timer_start_at(when); }

void platform_timer_start_after(platform_time_t delta_ticks)
{
  timer_start_after(delta_ticks);
}

/* ========== RTC ========== */

void platform_rtc_init(void) { goldfish_rtc_init(); }

uint64_t platform_rtc_read_ns(void) { return goldfish_rtc_read_ns(); }

void platform_rtc_set_alarm_after(uint64_t delay_ns)
{
  goldfish_rtc_set_alarm_after(delay_ns);
}

/* ========== IRQ handler 注册表 ========== */

#define MAX_IRQ 64

typedef struct {
  irq_handler_t handler;
  void* arg;
} irq_entry_t;

typedef struct {
  uint64_t count;              // 触发次数
  platform_time_t last_tick;   // 上次触发的 tick
  platform_time_t first_tick;  // 第一次触发
  platform_time_t max_delta;   // 相邻两次最大间隔（可选）
} irq_stat_t;

static irq_entry_t s_irq_table[MAX_IRQ];
static irq_stat_t s_irq_stats[MAX_IRQ];
static const char* s_irq_name[MAX_IRQ];

static void platform_irq_table_init(void)
{
  for (int i = 0; i < MAX_IRQ; ++i) {
    s_irq_table[i].handler = NULL;
    s_irq_table[i].arg     = NULL;
  }
}

void platform_register_irq_handler(uint32_t irq, irq_handler_t handler,
                                   void* arg, const char* name)
{
  if (irq >= MAX_IRQ) {
    return;
  }

  s_irq_table[irq].handler = handler;
  s_irq_table[irq].arg     = arg;
  s_irq_name[irq]          = name;

  plic_set_priority(irq, 1);
  plic_enable_irq(irq);
}

void platform_handle_s_external(struct trapframe* tf)
{
  (void)tf;
  for (;;) {
    uint32_t irq = plic_claim();
    if (!irq) break;

    platform_time_t now = platform_time_now();

    if (irq < MAX_IRQ) {
      irq_stat_t* st = &s_irq_stats[irq];
      if (st->count == 0) {
        st->first_tick = now;
      } else {
        platform_time_t delta = now - st->last_tick;
        if (delta > st->max_delta) st->max_delta = delta;
      }
      st->last_tick = now;
      st->count++;
    }

    irq_handler_t handler = NULL;
    void* arg             = NULL;
    if (irq < MAX_IRQ) {
      handler = s_irq_table[irq].handler;
      arg     = s_irq_table[irq].arg;
    }

    if (handler) {
      handler(irq, arg);
    } else {
      platform_puts("unknown PLIC irq\n");
    }

    plic_complete(irq);
  }
}

/* ========== PLIC & IRQ ========== */

void platform_plic_init(void)
{
  // 1. S-mode PLIC context
  plic_init_s_mode();

  // 2. 开 UART0 RTC 中断
  uint32_t uart_irq = uart16550_get_irq();
  platform_register_irq_handler(uart_irq, uart16550_irq_handler, NULL, "uart0");

  uint32_t rtc_irq = goldfish_rtc_get_irq();
  platform_register_irq_handler(rtc_irq, goldfish_rtc_irq_handler, NULL,
                                "rtc0");
}

void platform_init(uintptr_t hartid, uintptr_t dtb_pa)
{
  platform_set_dtb(dtb_pa);

  platform_uart_init();
  platform_rtc_init();
  platform_timer_init(hartid);

  platform_irq_table_init();
  platform_plic_init();
}

/* ========== MISC ========== */

size_t platform_irq_get_stats(platform_irq_stat_t* out, size_t max)
{
  if (!out) return 0;

  size_t n = (max < MAX_IRQ) ? max : MAX_IRQ;
  for (size_t i = 0; i < n; ++i) {
    out[i].irq        = (uint32_t)i;
    out[i].count      = s_irq_stats[i].count;
    out[i].first_tick = s_irq_stats[i].first_tick;
    out[i].last_tick  = s_irq_stats[i].last_tick;
    out[i].max_delta  = s_irq_stats[i].max_delta;
    out[i].name       = s_irq_name[i];
  }
  return n;
}

void platform_idle(void)
{
  __asm__ volatile("nop");
  // __asm__ volatile("wfi");
}
