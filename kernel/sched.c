/* sched.c */

#include "cpu.h"
#include "platform.h"
#include "riscv_csr.h"
#include "runqueue.h"
#include "sched.h"
#include "thread.h"
#include "trap.h"

/*
 * Phase1：硬抢占 + 每核本地 tick
 *   - 每个 hart 都 re-arm 自己的 timer（硬抢占来源）。
 *   - 只有 boot hart 推进 threads_tick()，维持全局 sleep 语义。
 *   - 时间片：SCHED_SLICE_TICKS 次 tick 后触发 schedule()。
 *   - IPI：仅用于立即 resched，不承担长期时间片职责。
 */

static inline void sched_rearm_timer(void) {
  platform_timer_start_after(platform_sched_delta_ticks());
}

void
sched_init_this_hart(uint32_t hartid)
{
  (void)hartid;
  cpu_t *c        = cpu_this();
  c->need_resched = 0;
  c->slice_left   = SCHED_SLICE_TICKS;
}

void
sched_on_timer_irq(struct trapframe *tf)
{
  cpu_t *c = cpu_this();
  c->timer_irqs++;

  /* 先重设本地 timer，保证下次 tick 会来。 */
  sched_rearm_timer();

  /* 只有 boot hart 负责推进全局时间 / 唤醒睡眠线程。 */
  if (c->hartid == g_boot_hartid) {
    threads_tick();
  }

  /* 时间片计数：到零才触发 schedule，避免过于频繁的切换。 */
  if (c->slice_left > 0) {
    c->slice_left--;
  }
  if (c->slice_left == 0) {
    c->slice_left   = SCHED_SLICE_TICKS;
    c->need_resched = 0;  /* 正在执行 schedule，无需额外 resched 标记 */
    schedule(tf);
  }
}

void
sched_on_ipi_irq(struct trapframe *tf)
{
  /* 清除 SSIP，避免下一次进 trap 看到 pending。 */
  csr_clear(sip, SIP_SSIP);

  cpu_t *c        = cpu_this();
  c->need_resched = 0;  /* 即将 schedule，不需要累积 */
  schedule(tf);
}

uint32_t
sched_pick_target_hart(tid_t tid, uint32_t waker_hart)
{
  (void)tid;
  uint32_t best_hart   = waker_hart;
  uint32_t best_len    = rq_len(waker_hart);
  uint32_t best_online = g_cpus[waker_hart].online;

  for (uint32_t h = 0; h < (uint32_t)MAX_HARTS; ++h) {
    if (!g_cpus[h].online) continue;
    uint32_t len = rq_len(h);
    if (!best_online || len < best_len) {
      best_hart   = h;
      best_len    = len;
      best_online = 1;
    }
  }
  return best_hart;
}
