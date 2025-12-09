#include "plic.h"
#include "platform.h"    // platform_get_dtb()
#include "fdt_helper.h"  // fdt_find_reg_by_compat()

static uintptr_t plic_base;  // runtime PLIC MMIO base

static inline void w32(uint32_t off, uint32_t v)
{
  *(volatile uint32_t *)(plic_base + off) = v;
}

static inline uint32_t r32(uint32_t off)
{
  return *(volatile uint32_t *)(plic_base + off);
}

/*
 * 确保 plic_base 已经从 FDT 初始化。
 * 会在第一次被调用时从 device tree 里解析 PLIC 的 reg。
 */
static void plic_ensure_base(void)
{
  if (plic_base != 0) {
    return;
  }

  const void *fdt = platform_get_dtb();
  if (!fdt) {
    // 理论上 kernel_main 一开始就已经 set 过 dtb
    return;
  }

  uint64_t base, size;

  // QEMU virt 的 PLIC compatible 可能是 "riscv,plic0" 或 "sifive,plic-1.0.0"
  if (fdt_find_reg_by_compat(fdt, "riscv,plic0", &base, &size) < 0 &&
      fdt_find_reg_by_compat(fdt, "sifive,plic-1.0.0", &base, &size) < 0) {
    // 找不到就保持 plic_base = 0，上层调用最好检查是否有 PLIC
    return;
  }

  plic_base = (uintptr_t)base;
}

void plic_init_s_mode(void)
{
  plic_ensure_base();
  if (plic_base == 0) {
    return;  // 没有 PLIC，或者 FDT 解析失败
  }

  // S-mode threshold = 0，允许所有优先级的中断
  w32(PLIC_STHRESHOLD_HART0_OFFSET, 0);

  // 一开始关掉所有 S-mode 使能，具体设备谁需要中断谁自己开
  w32(PLIC_SENABLE_HART0_OFFSET, 0);
}

void plic_set_priority(uint32_t irq, uint32_t prio)
{
  if (irq == 0) return;  // 0 号 IRQ 保留
  plic_ensure_base();
  if (plic_base == 0) return;

  w32(PLIC_PRIORITY_OFFSET + 4u * irq, prio);
}

void plic_enable_irq(uint32_t irq)
{
  if (irq >= 32) return;  // 目前只用前 32 个 IRQ，够 UART/RTC/virtio 之类
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t en = r32(PLIC_SENABLE_HART0_OFFSET);
  en |= (1u << irq);
  w32(PLIC_SENABLE_HART0_OFFSET, en);
}

void plic_disable_irq(uint32_t irq)
{
  if (irq >= 32) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t en = r32(PLIC_SENABLE_HART0_OFFSET);
  en &= ~(1u << irq);
  w32(PLIC_SENABLE_HART0_OFFSET, en);
}

uint32_t plic_claim(void)
{
  plic_ensure_base();
  if (plic_base == 0) return 0;

  return r32(PLIC_SCLAIM_HART0_OFFSET);
}

void plic_complete(uint32_t irq)
{
  if (!irq) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  w32(PLIC_SCLAIM_HART0_OFFSET, irq);
}
