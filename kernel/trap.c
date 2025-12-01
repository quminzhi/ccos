// kernel/trap.c

#include <stdint.h>
#include "riscv_csr.h"
#include "platform.h"
#include "log.h"  // 提供 pr_err / pr_debug 等

/*
 * trap_entry_c 由 arch/riscv/trap.S 调用：
 *
 * trap_entry:
 *   保存最小现场 (ra 等)
 *   call trap_entry_c
 *   恢复现场
 *   sret
 *
 * 注意：为了让 pr_err 能正常输出，务必在启用中断前调用 log_init_baremetal()
 * （或者至少在可能触发 trap 的路径之前），否则 writer
 * 还没初始化时日志会被丢弃。
 */
void trap_entry_c(void)
{
  reg_t scause = csr_read(scause);
  reg_t stval  = csr_read(stval);
  reg_t code   = mcause_code(scause);

  if (mcause_is_interrupt(scause)) {
    /* S-mode timer interrupt */
    if (code == IRQ_TIMER_S) {
      platform_handle_timer_interrupt();
      return;
    }

    /* 其他中断暂时没处理，用 pr_err 打印出来 */
    pr_err("unhandled interrupt: code=%llu scause=0x%llx",
           (unsigned long long)code, (unsigned long long)scause);
  } else {
    /* 异常：打印 scause/stval，方便调试 */
    pr_err("exception: code=%llu scause=0x%llx stval=0x%llx",
           (unsigned long long)code, (unsigned long long)scause,
           (unsigned long long)stval);
  }

  /* 当前阶段：遇到未处理 trap 直接停机 */
  for (;;) {
    __asm__ volatile("wfi");
  }
}
