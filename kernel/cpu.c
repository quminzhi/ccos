#include "cpu.h"
#include <stdint.h>
#include "log.h"
#include "panic.h"
#include "arch.h"
#include "thread.h"
#include "riscv_csr.h"

cpu_t g_cpus[MAX_HARTS];
uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

volatile uint32_t g_boot_hartid = NO_BOOT_HART;
volatile int smp_boot_done      = 0;

void cpu_init_this_hart(uintptr_t hartid) {
  if (hartid >= MAX_HARTS) {
    PANICF("hartid %lu >= MAX_HARTS", (unsigned long)hartid);
  }

  cpu_t *c        = &g_cpus[hartid];

  /* 先填最关键的字段（trap / 调试可能马上会用到） */
  c->hartid       = (uint32_t)hartid;
  c->kstack_top   = cpu_kstack_top((uint32_t)hartid);
  c->cur_tf       = 0;

  c->idle_tid     = (tid_t)hartid;
  c->current_tid  = (tid_t)-1;

  c->timer_irqs   = 0;
  c->ctx_switches = 0;

  /* tp / sscratch 永远指向 cpu_t */
  __asm__ volatile("mv tp, %0" ::"r"(c) : "memory");
  csr_write_sscratch((uintptr_t)c);

  /* online 建议：等真正进入 idle/调度点再置 1 更合理
   * 但你现在先置 1 也能跑
   */
  c->online = 1;
}

void cpu_enter_idle(uint32_t hartid) {
  ASSERT(hartid < MAX_HARTS);

  cpu_t *c = cpu_this();
  ASSERT(c && c->hartid == hartid);

  // 建议：确保本 hart 还没开中断再进（至少 SIE=0）
  // uint32_t flags = irq_disable();  // 如果你有的话

  /* 约定：tid == hartid 是该 CPU 的 idle */
  c->idle_tid    = (tid_t)hartid;
  c->current_tid = c->idle_tid;

  /* cur_tf 指向“当前要运行的线程”的 trapframe（trap.S 会用到） */
  Thread *idle   = &g_threads[c->idle_tid];
  c->cur_tf      = &idle->tf;

  /* 初始状态：本 CPU 上的 idle 正在跑 */
  idle->state    = THREAD_RUNNING;

  /* 不会返回：直接按 idle->tf 的 sstatus/sepc sret */
  arch_first_switch(&idle->tf);

  __builtin_unreachable();
}

void set_smp_boot_done() {
  smp_mb();  // 确保上面所有写操作先对其它 hart 可见
  smp_boot_done = 1;
  smp_mb();  // 防止之后的代码被重排到前面
}

void wait_for_smp_boot_done() {
  while (!smp_boot_done) {
    smp_mb();
    __asm__ volatile("wfi");
  }
  smp_mb();
}
