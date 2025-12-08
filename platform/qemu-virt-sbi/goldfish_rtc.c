// goldfish_rtc.c
#include "goldfish_rtc.h"
#include <stdint.h>
#include "platform.h"
#include "fdt_helper.h"

// 寄存器偏移
#define RTC_TIME_LOW        0x00
#define RTC_TIME_HIGH       0x04
#define RTC_ALARM_LOW       0x08
#define RTC_ALARM_HIGH      0x0c
#define RTC_IRQ_ENABLED     0x10
#define RTC_CLEAR_ALARM     0x14
#define RTC_ALARM_STATUS    0x18
#define RTC_CLEAR_INTERRUPT 0x1c

// 硬编码版本：
// // QEMU virt.dts 里的地址：reg = <0x0 0x00101000 0x0 0x00001000>
// #define QEMU_VIRT_RTC_BASE  0x00101000UL
// // PLIC 里的 IRQ 号：virt.dts 里 rtc@101000 的 interrupts = <11>;
// #define QEMU_VIRT_RTC_IRQ   11
// static volatile uint32_t *rtc_base = (volatile uint32_t *)QEMU_VIRT_RTC_BASE;

static volatile uint32_t *rtc_base;
static uint32_t rtc_irq;

static inline uint32_t rtc_r32(uint32_t off) { return rtc_base[off / 4]; }

static inline void rtc_w32(uint32_t off, uint32_t val)
{
  rtc_base[off / 4] = val;
}

uint32_t goldfish_rtc_get_irq(void) {
  return rtc_irq;
}

void goldfish_rtc_init(void)
{
  const void *fdt = platform_get_dtb();
  uint64_t base, size;
  uint32_t irq;

  if (fdt_find_reg_by_compat(fdt, "google,goldfish-rtc", &base, &size) < 0) {
    platform_puts("goldfish_rtc_init: no goldfish-rtc in fdt\n");
    return;
  }

  if (fdt_find_irq_by_compat(fdt, "google,goldfish-rtc", &irq) < 0) {
    platform_puts("goldfish_rtc_init: no interrupts for goldfish-rtc\n");
    return;
  }

  rtc_base = (volatile uint32_t *)(uintptr_t)base; // 暂时物理=虚拟
  rtc_irq  = irq;

  // 清一下可能遗留的状态
  rtc_w32(RTC_CLEAR_ALARM, 1);
  rtc_w32(RTC_CLEAR_INTERRUPT, 1);

  // 先默认不开中断，等真正设置闹钟的时候再 enable
  rtc_w32(RTC_IRQ_ENABLED, 0);

  // 注册 handler
  platform_register_irq_handler(rtc_irq, goldfish_rtc_irq_handler);
}

uint64_t goldfish_rtc_read_ns(void)
{
  // 文档要求：先读 TIME_LOW，再读 TIME_HIGH，这样 QEMU 会给你一个原子 snapshot
  uint32_t lo = rtc_r32(RTC_TIME_LOW);
  uint32_t hi = rtc_r32(RTC_TIME_HIGH);
  return ((uint64_t)hi << 32) | lo;
}

void goldfish_rtc_set_alarm_after(uint64_t delay_ns)
{
  uint64_t now  = goldfish_rtc_read_ns();
  uint64_t when = now + delay_ns;

  uint32_t lo   = (uint32_t)(when & 0xffffffffu);
  uint32_t hi   = (uint32_t)(when >> 32);

  /*
   * ⚠️ 注意顺序：QEMU 代码里是：
   *   - 写 ALARM_HIGH：只改 high 32 位，不触发 set_alarm()
   *   - 写 ALARM_LOW：改 low 32 位，并调用 goldfish_rtc_set_alarm()
   *
   * 所以必须先写 HIGH，再写 LOW，这样 set_alarm() 看到的 alarm_next
   * 才是完整的 64bit 值。
   */
  rtc_w32(RTC_ALARM_HIGH, hi);
  rtc_w32(RTC_ALARM_LOW, lo);  // 这一步才真正 arm timer

  // 打开 IRQ，让 irq_pending 生效
  rtc_w32(RTC_IRQ_ENABLED, 1);
}

void goldfish_rtc_irq_handler(void)
{
  /*
   * 这里已经是“RTC 这个 IRQ 的 handler”了，不需要再读 ALARM_STATUS。
   *
   * QEMU 里：
   *   - 中断触发时：alarm_running = 0; irq_pending = 1;
   *   - ALARM_STATUS 返回的是 alarm_running（闹钟是否还在跑），
   *     在中断触发那一刻已经被置 0，所以读出来必然是 0。
   *
   * 真正决定 IRQ 线是否拉高的是 irq_pending & irq_enabled。
   * 要结束这次中断，只需要 CLEAR_INTERRUPT 把 irq_pending 清零。
   */

  rtc_w32(RTC_CLEAR_INTERRUPT, 1);

  // 这句现在已经可以正常看到
  platform_puts("goldfish rtc irq hit\n");

  // 如果你希望一次性闹钟用完就关掉中断，可以顺便关掉：
  // rtc_w32(RTC_IRQ_ENABLED, 0);
}
