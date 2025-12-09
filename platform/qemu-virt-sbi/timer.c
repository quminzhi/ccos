#include "timer.h"
#include "fdt_helper.h"
#include "riscv_csr.h"
#include "sbi.h"

// 在 “OpenSBI + S-mode kernel” 这个架构下，CLINT 这片地址空间是 M 态私有的，
// S 态不能直接读写 0x0200_0000 那一段。
// CLINT/ACLINT node 是给 M 态（OpenSBI）看的，S 态只能用 SBI。
// “CLINT 后端”属于 “直接摸 CLINT MMIO 的玩法”，在当前架构下是非法的。
// 我们先注释CLINT后端，给自定义M模式预留。

// typedef enum {
//   TIMER_BACKEND_SBI   = 0,
//   TIMER_BACKEND_CLINT = 1,
// } timer_backend_t;

// static timer_backend_t timer_backend = TIMER_BACKEND_SBI;

/* CLINT 相关寄存器指针（QEMU virt 的 CLINT/ACLINT 布局兼容 SiFive CLINT） */
// static volatile uint64_t *clint_mtime     = NULL;
// static volatile uint64_t *clint_mtimecmp  = NULL;

// QEMU virt 上 CLINT/ACLINT 兼容 SiFive CLINT 布局
// base + 0x0000  : msip[hart0]
// ...
// base + 0x4000  : mtimecmp[hart0]
// ...
// base + 0xbff8  : mtime (64bit)
#define CLINT_MTIMECMP_BASE 0x4000u
#define CLINT_MTIME_BASE    0xBFF8u

void timer_init(uintptr_t hartid)
{
  (void)hartid;

  const void *fdt = platform_get_dtb();
  if (!fdt) {
    platform_puts("timer: no FDT, fallback to SBI\n");
    // timer_backend = TIMER_BACKEND_SBI;
    return;
  }

  uint64_t base = 0, size = 0;
  int found = 0;

  // 1. 优先尝试 ACLINT MTIMER
  if (fdt_find_reg_by_compat(fdt, "riscv,aclint-mtimer", &base, &size) == 0) {
    platform_puts("timer: found riscv,aclint-mtimer\n");
    found = 1;
  }

  // 2. 不行就退回老的 CLINT 兼容串
  if (!found &&
      fdt_find_reg_by_compat(fdt, "sifive,clint0", &base, &size) != 0 &&
      fdt_find_reg_by_compat(fdt, "riscv,clint0", &base, &size) != 0) {
    platform_puts("timer: no CLINT/ACLINT in FDT, use SBI\n");
    // timer_backend = TIMER_BACKEND_SBI;
    return;
  }

  // uintptr_t clint_base = (uintptr_t)base;

  // clint_mtime          = (volatile uint64_t *)(clint_base +
  // CLINT_MTIME_BASE); clint_mtimecmp =
  //     (volatile uint64_t *)(clint_base + CLINT_MTIMECMP_BASE + 8u * hartid);

  // timer_backend = TIMER_BACKEND_CLINT;

  // platform_puts("timer: using CLINT backend\n");
}

platform_time_t timer_now(void)
{
  // if (timer_backend == TIMER_BACKEND_CLINT && clint_mtime) {
  //   // platform_puts("TIMER_BACKEND_CLINT\n");
  //   return *clint_mtime;
  // }

  return csr_read(time);
}

void timer_start_at(platform_time_t when)
{
  // if (timer_backend == TIMER_BACKEND_CLINT && clint_mtimecmp) {
  //   *clint_mtimecmp = when;
  // } else {
  sbi_set_timer(when);
  // }
}

void timer_start_after(platform_time_t delta_ticks)
{
  platform_time_t now = timer_now();
  timer_start_at(now + delta_ticks);
}
