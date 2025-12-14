#include "cpu.h"
#include <stdint.h>
#include "log.h"
#include "arch.h"
#include "thread.h"
#include "riscv_csr.h"
#include "platform.h"
#include "sbi.h"
#include "sched.h"

enum {
  HART_BITS_PER_WORD = (int)(sizeof(unsigned long) * 8u),
  HART_MASK_WORDS =
      (MAX_HARTS + HART_BITS_PER_WORD - 1) / HART_BITS_PER_WORD,
};

cpu_t g_cpus[MAX_HARTS];
uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

volatile uint32_t g_boot_hartid = NO_BOOT_HART;
volatile int smp_boot_done      = 0;

/* 供 IPI 使用的全局 hart mask 缓冲区（放在 BSS，M 态可直接访问）。 */
static unsigned long g_smp_ipi_mask[HART_MASK_WORDS] __attribute__((aligned(sizeof(unsigned long))));

static inline void smp_set_online(uint32_t hartid) {
  g_cpus[hartid].online = 1;
}

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
  smp_set_online((uint32_t)hartid);

  /* 依赖 tp 指向当前 cpu 的 sched 初始化（时间片、need_resched 等）。 */
  sched_init_this_hart((uint32_t)hartid);
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
  thread_mark_running(idle, hartid);

  /*
   * 现在 cpu->cur_tf 已经指向 idle 的 trapframe，可以放心打开中断，
   * 否则 trap_entry 会在 .Lno_tf 忙等。
   *
   * SMP 调度策略（当前版本）：
   *   - 每个 hart 都设置自己的 timer tick（硬抢占用）。
   *   - boot hart 在 tick 里推进全局时间 / 唤醒 SLEEPING。
   *   - 任意 hart 把线程变 RUNNABLE 时，若目标 hart 不是自己则发 IPI（SSIP）。
   *   - 所有 hart 必须打开 SSIP/SEIP/STIP。
   */
  platform_timer_start_after(DELTA_TICKS);
  arch_enable_timer_interrupts();
  arch_enable_external_interrupts();
  arch_enable_software_interrupts();

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

void smp_kick_all_others(void) {
  if (!smp_boot_done) {
    return;
  }

  const uint32_t self = cpu_current_hartid();
  /* Send one hart at a time to avoid multi-bit mask validation paths. */
  for (uint32_t hart = 0; hart < (uint32_t)MAX_HARTS; ++hart) {
    if (hart == self) continue;
    if (!g_cpus[hart].online) continue;

    /* Base is the target hart; bit0 hits that hart. */
    struct sbiret ret = sbi_send_ipi(1UL, hart);
    if (ret.error) {
      pr_warn("sbi_send_ipi failed: err=%ld target=%u mask=0x%lx\n", ret.error,
              hart, 1UL);
    }
  }
}

void smp_kick_hart(uint32_t hartid) {
  if (!smp_boot_done) return;
  if (hartid >= (uint32_t)MAX_HARTS) return;
  if (!g_cpus[hartid].online) return;

  struct sbiret ret = sbi_send_ipi(1UL, hartid);
  if (ret.error) {
    pr_warn("sbi_send_ipi failed: err=%ld target=%u\n", ret.error, hartid);
  }
}
