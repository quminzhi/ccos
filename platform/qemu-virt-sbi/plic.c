#include "plic.h"
#include "platform.h"    // platform_get_dtb()
#include "fdt_helper.h"  // fdt_find_reg_by_compat()
#include "cpu.h"         // cpu_current_hartid()

static uintptr_t plic_base;  // runtime PLIC MMIO base

static inline void w32(uint32_t off, uint32_t v) {
  *(volatile uint32_t *)(plic_base + off) = v;
}

static inline uint32_t r32(uint32_t off) {
  return *(volatile uint32_t *)(plic_base + off);
}

/*
 * QEMU virt 上 PLIC 的 S-mode context 布局（简化模型）：
 *
 *   - 每个 hart 有两个 context：M-mode + S-mode
 *   - context 之间是等间隔的：
 *       enable:    0x80   bytes/ctx
 *       threshold: 0x1000 bytes/ctx
 *   - 我们已经有了 “hart0 S-mode 的 offset” 宏：
 *       PLIC_SENABLE_HART0_OFFSET
 *       PLIC_STHRESHOLD_HART0_OFFSET
 *       PLIC_SCLAIM_HART0_OFFSET
 *
 *   于是可以认为：
 *     hartN 的 S-mode context = hart0 的 S-mode context + N * stride_per_hart
 *   其中 stride_per_hart = 2 * stride_per_context  （因为每 hart 有 M+S 两个
 * context）
 */

enum {
  PLIC_CONTEXTS_PER_HART         = 2u,
  PLIC_ENABLE_PER_CONTEXT_STRIDE = 0x80u,
  PLIC_CONTEXT_STRIDE            = 0x1000u,
};

static inline uint32_t plic_senable_offset_for_hart(uint32_t hartid) {
  uint32_t delta_ctx = hartid * PLIC_CONTEXTS_PER_HART;
  return PLIC_SENABLE_HART0_OFFSET + delta_ctx * PLIC_ENABLE_PER_CONTEXT_STRIDE;
}

static inline uint32_t plic_sthreshold_offset_for_hart(uint32_t hartid) {
  uint32_t delta_ctx = hartid * PLIC_CONTEXTS_PER_HART;
  return PLIC_STHRESHOLD_HART0_OFFSET + delta_ctx * PLIC_CONTEXT_STRIDE;
}

static inline uint32_t plic_sclaim_offset_for_hart(uint32_t hartid) {
  uint32_t delta_ctx = hartid * PLIC_CONTEXTS_PER_HART;
  return PLIC_SCLAIM_HART0_OFFSET + delta_ctx * PLIC_CONTEXT_STRIDE;
}

/*
 * 确保 plic_base 已经从 FDT 初始化。
 * 会在第一次被调用时从 device tree 里解析 PLIC 的 reg。
 */
static void plic_ensure_base(void) {
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

/*
 * 初始化“当前 hart”的 S-mode PLIC context：
 *   - threshold = 0（允许所有优先级）
 *   - 先禁用所有中断，具体设备再逐个 enable
 */
void plic_init_s_mode(void) {
  plic_ensure_base();
  if (plic_base == 0) {
    return;  // 没有 PLIC，或者 FDT 解析失败
  }

  uint32_t hartid = cpu_current_hartid();
  uint32_t th_off = plic_sthreshold_offset_for_hart(hartid);
  uint32_t en_off = plic_senable_offset_for_hart(hartid);

  // S-mode threshold = 0，允许所有优先级的中断
  w32(th_off, 0);

  // 一开始关掉所有 S-mode 使能，具体设备谁需要中断谁自己开
  w32(en_off, 0);
}

/* ---- IRQ source 相关（全局） ------------------------------------------ */

void plic_set_priority(uint32_t irq, uint32_t prio) {
  if (irq == 0) return;  // 0 号 IRQ 保留
  plic_ensure_base();
  if (plic_base == 0) return;

  w32(PLIC_PRIORITY_OFFSET + 4u * irq, prio);
}

/*
 * 为“当前 hart”的 S-mode context 打开 / 关闭某个 IRQ。
 * 目前我们只使用前 32 个 IRQ，足够 UART / RTC / 简单 virtio。
 */

void plic_enable_irq(uint32_t irq) {
  if (irq >= 32) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t hartid = cpu_current_hartid();
  uint32_t en_off = plic_senable_offset_for_hart(hartid);

  uint32_t en     = r32(en_off);
  en |= (1u << irq);
  w32(en_off, en);
}

void plic_disable_irq(uint32_t irq) {
  if (irq >= 32) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t hartid = cpu_current_hartid();
  uint32_t en_off = plic_senable_offset_for_hart(hartid);

  uint32_t en     = r32(en_off);
  en &= ~(1u << irq);
  w32(en_off, en);
}

/*
 * claim / complete 也改成按“当前 hart”的 S-mode context 来访问。
 * 这样无论 boot hart 是谁，外部中断都会从正确的 context 被取走。
 */

uint32_t plic_claim(void) {
  plic_ensure_base();
  if (plic_base == 0) return 0;

  uint32_t hartid = cpu_current_hartid();
  uint32_t cl_off = plic_sclaim_offset_for_hart(hartid);

  return r32(cl_off);
}

void plic_complete(uint32_t irq) {
  if (!irq) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t hartid = cpu_current_hartid();
  uint32_t cl_off = plic_sclaim_offset_for_hart(hartid);

  w32(cl_off, irq);
}
