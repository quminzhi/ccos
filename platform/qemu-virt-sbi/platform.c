/* platform/qemu-virt-sbi/platform.c */

#include <stdint.h>
#include <stddef.h>
#include "log.h"
#include "riscv_csr.h"
#include "uart_16550.h"
#include "platform.h"
#include "plic.h"
#include "timer.h"
#include "panic.h"
#include "libfdt.h"

static const void* g_dtb;  /* Cached DTB pointer */
static uint32_t g_timebase_hz;   /* Cached /cpus/timebase-frequency */

const void* platform_get_dtb(void) {
  return g_dtb;
}

void platform_set_dtb(uintptr_t dtb_pa) {
  const void* new_dtb = (const void*)dtb_pa;

  if (!new_dtb) {
    return;
  }

  if (g_dtb == NULL) {
    g_dtb = new_dtb;
    return;
  }

  if (g_dtb != new_dtb) {
    panic("platform_set_dtb: dtb mismatch");
  }
}

uint32_t platform_timebase_hz(void) {
  if (g_timebase_hz) return g_timebase_hz;

  uint32_t hz = 0;
  if (g_dtb) {
    int off = fdt_path_offset(g_dtb, "/cpus");
    if (off >= 0) {
      int len = 0;
      const fdt32_t* p = (const fdt32_t*)fdt_getprop(g_dtb, off,
                                                     "timebase-frequency",
                                                     &len);
      if (p && len >= (int)sizeof(fdt32_t)) {
        hz = fdt32_to_cpu(p[0]);
      }
    }
  }
  if (hz == 0) {
    hz = 10000000u; /* Typical default: 10MHz (QEMU virt uses this) */
  }
  g_timebase_hz = hz;
  return hz;
}

/* ========== Console output helpers ========== */

void platform_uart_init() {
  uart16550_init();
}

void platform_put_dec_us(uint64_t x);

void platform_put_dec_s(int64_t v) {
  if (v < 0) {
    /* Print sign */
    platform_write("-", 1);

    /* Handle absolute value; beware INT64_MIN */
    uint64_t mag = (uint64_t)(-(v + 1)) + 1;
    platform_put_dec_us(mag);
  } else {
    platform_put_dec_us((uint64_t)v);
  }
}

void platform_put_dec_us(uint64_t x) {
  /* uint64_t max is 18446744073709551615, 20 decimal digits */
  char buf[20 + 1];  /* 20 digits + '\0' */
  int pos = 0;

  if (x == 0) {
    buf[pos++] = '0';
  } else {
    char tmp[20];
    int len = 0;

    /* Store digits in reverse order */
    while (x > 0) {
      uint64_t q = x / 10;
      uint32_t r = (uint32_t)(x - q * 10);  /* x % 10 without another div */
      tmp[len++] = (char)('0' + r);
      x          = q;
    }

    /* Then reverse into buf */
    for (int i = len - 1; i >= 0; --i) {
      buf[pos++] = tmp[i];
    }
  }

  buf[pos] = '\0';
  platform_write(buf, (size_t)pos);
}

void platform_put_hex64(uint64_t x) {
  char buf[2 + 16 + 1];  /* "0x" + 16 hex + '\0' */
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

void platform_write(const char* buf, size_t len) {
  if (!buf || len == 0) return;

  uart16550_write(buf, len);
}

void platform_putc(char c) {
  uart16550_putc(c);
}

void platform_puts(const char* s) {
  if (!s) return;

  uart16550_puts(s);
}

/* ========== Timer helpers ========== */

void platform_timer_init(uintptr_t hartid) {
  timer_init(hartid);
}

platform_time_t platform_time_now(void) {
  return timer_now();
}

void platform_timer_start_at(platform_time_t when) {
  timer_start_at(when);
}

void platform_timer_start_after(platform_time_t delta_ticks) {
  timer_start_after(delta_ticks);
}

platform_time_t platform_sched_delta_ticks(void) {
  /* Default: ~1ms interval based on timebase-frequency */
  uint32_t hz = platform_timebase_hz();
  platform_time_t ticks = (platform_time_t)(hz / 1000u);
  if (ticks == 0) ticks = 1; /* floor at 1 tick */
  return ticks;
}

/* ========== RTC ========== */

void platform_rtc_init(void) {
}

uint64_t platform_rtc_read_ns(void) {
  static int fallback_logged = 0;

  /*
   * RTC via time CSR + timebase-frequency from FDT (or default 10MHz).
   * No external goldfish-rtc dependency.
   */
  uint32_t hz = platform_timebase_hz();

  if (!fallback_logged) {
    pr_info("platform_rtc_read_ns: using time CSR (hz=%u)", hz);
    fallback_logged = 1;
  }

  uint64_t ticks = csr_read(time);
  /*
   * Avoid __int128 division (would pull in __udivti3):
   *   ns = (ticks / hz) * 1e9 + (ticks % hz) * 1e9 / hz
   */
  uint64_t sec = ticks / (uint64_t)hz;
  uint64_t rem = ticks - sec * (uint64_t)hz;
  return sec * 1000000000ull + (rem * 1000000000ull) / (uint64_t)hz;
}

void platform_rtc_set_alarm_after(uint64_t delay_ns) {
  (void)delay_ns; /* no-op: no external RTC */
}

/* ========== IRQ handler 注册表 ========== */

#define MAX_IRQ 64

typedef struct {
  irq_handler_t handler;
  void* arg;
} irq_entry_t;

typedef struct {
  uint64_t count;              /* 触发次数 */
  platform_time_t last_tick;   /* 上次触发的 tick */
  platform_time_t first_tick;  /* 第一次触发 */
  platform_time_t max_delta;   /* 相邻两次最大间隔（可选） */
} irq_stat_t;

static irq_entry_t s_irq_table[MAX_IRQ];
static irq_stat_t s_irq_stats[MAX_IRQ];
static const char* s_irq_name[MAX_IRQ];

static void platform_irq_table_init(void) {
  for (int i = 0; i < MAX_IRQ; ++i) {
    s_irq_table[i].handler = NULL;
    s_irq_table[i].arg     = NULL;
  }
}

void platform_register_irq_handler(uint32_t irq, irq_handler_t handler,
                                   void* arg, const char* name) {
  if (irq >= MAX_IRQ) {
    return;
  }

  s_irq_table[irq].handler = handler;
  s_irq_table[irq].arg     = arg;
  s_irq_name[irq]          = name;

  plic_set_priority(irq, 1);
  plic_enable_irq(irq);
}

void platform_handle_s_external(struct trapframe* tf) {
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

void platform_plic_init(void) {
  /* 1. S-mode PLIC context */
  plic_init_s_mode();
}

void platform_init(uintptr_t hartid, uintptr_t dtb_pa) {
  platform_set_dtb(dtb_pa);

  platform_uart_init();
  platform_rtc_init();
  platform_timer_init(hartid);

  platform_plic_init();
  platform_irq_table_init();
}

void platform_boot_hart_init(uintptr_t hartid) {
  (void)hartid;
  ASSERT(g_dtb != NULL);

  uint32_t uart_irq = uart16550_get_irq();
  platform_register_irq_handler(uart_irq, uart16550_irq_handler, NULL, "uart0");
}

void platform_secondary_hart_init(uintptr_t hartid) {
  (void)hartid;
  ASSERT(g_dtb != NULL);

  /* 2. （可选）每核的 timer 初始化 */
  /* */
  /*    当前阶段：你的调度 tick 都跑在 boot hart 上，其它核基本只跑 idle + */
  /*    未来的 IPI。 所以可以先不在 secondary 上初始化 timer，等你做 per-CPU */
  /*    timer/调度时再打开。 */
  /* */
  /*    如果你之后把 timer 拆成 per-hart 的，这里可以写： */
  /*      platform_timer_init_this_hart(hartid); */
  /* */
  /*    现在先保守一点：不调用，避免和 boot hart 的用法搞混。 */
  /* platform_timer_init(hartid); */

  /* 3. 为“当前 hart”初始化自己的 S-mode PLIC context */
  /* */
  /*    这一步做的事情是： */
  /*      - threshold = 0（允许所有优先级中断） */
  /*      - SENABLE 清零（初始不使能任何 IRQ） */
  /* */
  /*    注意： */
  /*      - 不要在这里调用 platform_irq_table_init() */
  /*        否则会把 boot hart 注册好的 handler 清空。 */
  /*      - 不需要在这里重复注册 UART / RTC 的 handler； */
  /*        那些是“源级别”的，全局一套就够。 */
  /* */
  platform_plic_init();

  /* 4. 如果将来希望 secondary hart 也直接处理某些外设中断 */
  /*    （比如本地 virtio queue、per-CPU timer），你可以在这里： */
  /*      - 调用 plic_enable_irq(XXX); 打开该 hart 上的使能； */
  /*      - 或者新增一个类似 platform_plic_enable_irq_for_this_hart() 的封装。 */
  /* */
  /*    现在你的 UART 中断只需要送到 boot hart 跑 shell 就够了， */
  /*    secondary hart 主要先用于实验 SMP/IPI，所以这里可以先什么都不额外开。 */
}

/* ========== MISC ========== */

size_t platform_irq_get_stats(platform_irq_stat_t* out, size_t max) {
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

void platform_idle(void) {
  csr_set(sstatus, SSTATUS_SIE);
  __asm__ volatile("wfi");
}
