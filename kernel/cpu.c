#include <stdint.h>

#include "arch.h"
#include "cpu.h"
#include "log.h"
#include "platform.h"
#include "riscv_csr.h"
#include "sched.h"
#include "sbi.h"
#include "thread.h"

enum {
  HART_BITS_PER_WORD = (int)(sizeof(unsigned long) * 8u),
  HART_MASK_WORDS =
      (MAX_HARTS + HART_BITS_PER_WORD - 1) / HART_BITS_PER_WORD,
};

cpu_t g_cpus[MAX_HARTS];
uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

volatile uint32_t g_boot_hartid = NO_BOOT_HART;
volatile int smp_boot_done      = 0;

static inline void
smp_set_online(uint32_t hartid)
{
  g_cpus[hartid].online = 1;
  smp_mb();
}

void
cpu_init_this_hart(uintptr_t hartid)
{
  if (hartid >= MAX_HARTS) {
    PANICF("hartid %lu >= MAX_HARTS", (unsigned long)hartid);
  }

  cpu_t *c        = &g_cpus[hartid];

  /* Populate fields that trap/debug may use immediately */
  c->hartid       = (uint32_t)hartid;
  c->kstack_top   = cpu_kstack_top((uint32_t)hartid);
  c->cur_tf       = 0;

  c->idle_tid     = (tid_t)hartid;
  c->current_tid  = (tid_t)-1;

  c->timer_irqs   = 0;
  c->ctx_switches = 0;

  /* tp / sscratch always point to cpu_t */
  __asm__ volatile("mv tp, %0" ::"r"(c) : "memory");
  csr_write_sscratch((uintptr_t)c);

  /* online hint: ideally set when entering idle/scheduling;
   * setting it here also works.
   */
  smp_set_online((uint32_t)hartid);

  /* Scheduler init depends on tp pointing at the current cpu (slice, need_resched, etc.) */
  sched_init_this_hart((uint32_t)hartid);
}

void
cpu_enter_idle(uint32_t hartid)
{
  ASSERT(hartid < MAX_HARTS);

  cpu_t *c = cpu_this();
  ASSERT(c && c->hartid == hartid);

  /* Suggestion: ensure this hart has interrupts disabled before entering (at least SIE=0) */
  /* uint32_t flags = irq_disable(); use a global irq_disable if available */

  /* Convention: tid == hartid is the idle thread for this CPU */
  c->idle_tid    = (tid_t)hartid;
  c->current_tid = c->idle_tid;

  /* cur_tf points to the trapframe of the thread to run (trap.S uses it) */
  Thread *idle   = &g_threads[c->idle_tid];
  c->cur_tf      = &idle->tf;

  /* Initial state: this CPU's idle thread is running */
  idle->state    = THREAD_RUNNING;
  thread_mark_running(idle, hartid);

  /*
   * cpu->cur_tf now points to idle's trapframe, so it's safe to enable interrupts;
   * otherwise trap_entry would spin in .Lno_tf.
   *
   * SMP scheduling (current version):
   *   - Each hart arms its own timer tick (hard preemption).
  *   - Boot hart advances global time / wakes SLEEPING threads.
  *   - Any hart making a thread RUNNABLE sends SSIP if target hart is different.
  *   - All harts must enable SSIP/SEIP/STIP.
  */
  platform_timer_start_after(platform_sched_delta_ticks());
  arch_enable_timer_interrupts();
  arch_enable_external_interrupts();
  arch_enable_software_interrupts();

  /* Does not return: sret via idle->tf sstatus/sepc */
  arch_first_switch(&idle->tf);

  __builtin_unreachable();
}

void
set_smp_boot_done(void)
{
  smp_mb();  /* Ensure prior writes are visible to other harts */
  smp_boot_done = 1;
  smp_mb();  /* Prevent later code from being reordered before this point */
}

void
wait_for_smp_boot_done(void)
{
  while (!smp_boot_done) {
    smp_mb();
    __asm__ volatile("wfi");
  }
  smp_mb();
}

void
smp_kick_all_others(void)
{
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

void
smp_kick_hart(uint32_t hartid)
{
  if (!smp_boot_done) return;
  if (hartid >= (uint32_t)MAX_HARTS) return;
  if (!g_cpus[hartid].online) return;

  struct sbiret ret = sbi_send_ipi(1UL, hartid);
  if (ret.error) {
    pr_warn("sbi_send_ipi failed: err=%ld target=%u\n", ret.error, hartid);
  }
}

int
smp_wait_hart_online(uint32_t hartid, uint64_t timeout_ticks)
{
  if (hartid >= (uint32_t)MAX_HARTS) return -1;

  uint64_t start = platform_time_now();
  while ((platform_time_now() - start) < timeout_ticks) {
    if (*(volatile uint32_t *)&g_cpus[hartid].online) return 0;
  }
  return -1;
}
