#include <stdint.h>
#include <stddef.h>

#include "thread.h"
#include "klib.h"
#include "trap.h"
#include "log.h"
#include "platform.h"
#include "riscv_csr.h"

// arch.S
void arch_drop_to_user(void (*entry)(void *), void *arg, uintptr_t user_sp);

/* -------------------------------------------------------------------------- */
/* Configuration check                                                        */
/* -------------------------------------------------------------------------- */

#if THREAD_MAX < 2
#error "THREAD_MAX must be at least 2 (for idle + main)"
#endif

/* -------------------------------------------------------------------------- */
/* Types & globals                                                            */
/* -------------------------------------------------------------------------- */

typedef enum {
  THREAD_UNUSED   = 0,
  THREAD_RUNNABLE = 1,
  THREAD_RUNNING  = 2,
  THREAD_SLEEPING = 3,
  THREAD_WAITING  = 4, /* thread_join 中，等待其它线程 */
  THREAD_ZOMBIE   = 5, /* 已退出，等待 join 回收     */
} ThreadState;

typedef struct Thread {
  tid_t id;
  ThreadState state;
  uint64_t wakeup_tick; /* SLEEPING 时的唤醒 tick（绝对时间） */
  const char *name;

  struct trapframe tf; /* 保存的寄存器上下文 */

  uint8_t *stack_base; /* 栈底（main 用 boot 栈 -> NULL） */

  /* exit / join 相关 */
  int exit_code;             /* thread_exit(exit_code) 保存的值 */
  tid_t join_waiter;         /* 有谁在 join 我？（-1 表示没有） */
  tid_t waiting_for;         /* 我在 join 谁？（仅 WAITING 时有用） */
  uintptr_t join_status_ptr; /* join 时传入的 int*，保存 exit_code 用 */
} Thread;

static Thread g_threads[THREAD_MAX];
static uint8_t g_thread_stacks[THREAD_MAX][THREAD_STACK_SIZE];

static tid_t g_current_tid = 0;
static uint64_t g_ticks    = 0;

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

static inline void tf_clear(struct trapframe *tf)
{
  memset(tf, 0, sizeof(*tf));
}

static inline Thread *thread_by_tid(tid_t tid)
{
  if (tid < 0 || tid >= THREAD_MAX) {
    return NULL;
  }
  return &g_threads[tid];
}

/* 找空闲 slot（2 开始：0 idle，1 main 保留） */
static tid_t alloc_thread_slot(void)
{
  for (int i = 2; i < THREAD_MAX; ++i) {
    if (g_threads[i].state == THREAD_UNUSED) {
      return i;
    }
  }
  return -1;
}

/* 初始化线程上下文（栈 + entry + arg） */
static void init_thread_context(Thread *t, thread_entry_t entry, void *arg)
{
  tf_clear(&t->tf);

  uintptr_t sp = (uintptr_t)(t->stack_base + THREAD_STACK_SIZE);
  sp &= ~(uintptr_t)0xFUL; /* 16 字节对齐 */

  t->tf.sp   = sp;
  t->tf.sepc = (uintptr_t)entry;
  t->tf.a0   = (uintptr_t)arg;
}

/* idle 线程：简单 busy loop */
static void idle_main(void *arg)
{
  (void)arg;

  for (;;) {
    platform_idle();
  }
}

/* 回收已经被 join 的线程（把 slot 变回 UNUSED） */
static void recycle_thread(tid_t tid)
{
  if (tid <= 0 || tid >= THREAD_MAX) {
    return; /* 不回收 idle/main */
  }

  Thread *t          = &g_threads[tid];
  t->state           = THREAD_UNUSED;
  t->wakeup_tick     = 0;
  t->name            = "unused";
  t->exit_code       = 0;
  t->join_waiter     = -1;
  t->waiting_for     = -1;
  t->join_status_ptr = 0;
  tf_clear(&t->tf);
  /* 栈数组 g_thread_stacks[tid] 保留复用 */
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

/* Initialization and configure S-mode idle (tid = 0) */
void threads_init(void)
{
  g_ticks       = 0;
  g_current_tid = 1; /* main 线程假定为 tid=1 */

  for (int i = 0; i < THREAD_MAX; ++i) {
    g_threads[i].id              = i;
    g_threads[i].state           = THREAD_UNUSED;
    g_threads[i].wakeup_tick     = 0;
    g_threads[i].name            = "unused";
    g_threads[i].stack_base      = NULL;
    g_threads[i].exit_code       = 0;
    g_threads[i].join_waiter     = -1;
    g_threads[i].waiting_for     = -1;
    g_threads[i].join_status_ptr = 0;
    tf_clear(&g_threads[i].tf);
  }

  /* tid 0: idle (S-mode) */
  Thread *idle     = &g_threads[0];
  idle->state      = THREAD_RUNNABLE;
  idle->name       = "idle";
  idle->stack_base = g_thread_stacks[0];
  init_thread_context(idle, idle_main, NULL);

  reg_t s = csr_read(sstatus);  // 当前在 S 模式
  s |= SSTATUS_SPP;             // SPP=1 -> sret 后仍在 S
  s |= SSTATUS_SPIE;            // 允许 sret 后在 S 模式打开中断
  s &= ~SSTATUS_SIE;  // handler 里先关中断（按你现在 trap_init 的风格）
  idle->tf.sstatus   = s;

  /* tid 1: main (S-mode) */
  Thread *main_t     = &g_threads[1];
  main_t->state      = THREAD_RUNNING;
  main_t->name       = "main";
  main_t->stack_base = NULL; /* 使用 boot 栈 */
}

/* 创建新线程：返回 tid，失败返回 -1 */
tid_t thread_create_kern(thread_entry_t entry, void *arg, const char *name)
{
  if (!entry) {
    pr_info("thread_create: entry is NULL\n");
    return -1;
  }

  tid_t tid = alloc_thread_slot();
  if (tid < 0) {
    pr_warn("thread_create: no free slot\n");
    return -1;
  }

  Thread *t          = &g_threads[tid];

  t->state           = THREAD_RUNNABLE;
  t->wakeup_tick     = 0;
  t->name            = name ? name : "thread";
  t->stack_base      = g_thread_stacks[tid];
  t->exit_code       = 0;
  t->join_waiter     = -1;
  t->waiting_for     = -1;
  t->join_status_ptr = 0;

  init_thread_context(t, entry, arg);

  return tid;
}

void threads_start(thread_entry_t entry, void *arg)
{
  tid_t tid = alloc_thread_slot();
  if (tid < 0) {
    pr_warn("thread_start: no free slot\n");
  }
  Thread *t     = &g_threads[tid];

  /* 配 main 线程的基本信息和栈 */
  t->state      = THREAD_RUNNING;
  t->name       = "user_main";
  t->stack_base = g_thread_stacks[tid];

  /* 用已有的 helper 初始化它的 tf（sp / sepc / a0） */
  init_thread_context(t, entry, arg); /* tf.sp / tf.sepc / tf.a0 已经设置好 */

  /* 调用 arch_drop_to_user：从 S 模式掉到 U 模式，开始执行 entry(arg) */
  arch_drop_to_user((void (*)(void *))t->tf.sepc, (void *)t->tf.a0, t->tf.sp);

  /* 理论上不会返回；防止编译器抱怨，加个死循环 */
  while (1) {
    __asm__ volatile("wfi");
  }
}

/* 每个 timer tick 调用：更新 SLEEPING -> RUNNABLE */
void threads_tick(void)
{
  g_ticks++;

  for (int i = 0; i < THREAD_MAX; ++i) {
    Thread *t = &g_threads[i];
    if (t->state == THREAD_SLEEPING && t->wakeup_tick <= g_ticks) {
      t->wakeup_tick = 0;
      t->state       = THREAD_RUNNABLE;
    }
  }
}

/* 调度器：保存当前 tf，选下一个 RUNNABLE，恢复它的 tf */
void schedule(struct trapframe *tf)
{
  Thread *cur = &g_threads[g_current_tid];

  /* 保存当前上下文 */
  cur->tf     = *tf;
  if (cur->state == THREAD_RUNNING) {
    cur->state = THREAD_RUNNABLE;
  }

  /* 简单 round-robin */
  tid_t next_tid = -1;
  int start      = g_current_tid;

  for (int i = 1; i <= THREAD_MAX; ++i) {
    int cand = (start + i) % THREAD_MAX;
    if (g_threads[cand].state == THREAD_RUNNABLE) {
      next_tid = cand;
      break;
    }
  }

  if (next_tid < 0) {
    next_tid = 0; /* 退回 idle */
  }

  g_current_tid = next_tid;
  Thread *next  = &g_threads[next_tid];
  next->state   = THREAD_RUNNING;

  *tf           = next->tf;
}

/* -------------------------------------------------------------------------- */
/* Sleep / syscalls (kernel side)                                             */
/* -------------------------------------------------------------------------- */

void thread_sys_sleep(struct trapframe *tf, uint64_t ticks)
{
  Thread *cur = &g_threads[g_current_tid];

  if (ticks == 0) {
    /* sleep(0) = yield：不改状态，但交给调度器切一次 */
    schedule(tf);
    return;
  }

  cur->state       = THREAD_SLEEPING;
  cur->wakeup_tick = g_ticks + ticks;

  schedule(tf);
}

void thread_sys_create(struct trapframe *tf, thread_entry_t entry, void *arg,
                       const char *name)
{
  tid_t tid = thread_create_kern(entry, arg, name);
  tf->a0    = (uintptr_t)tid;
}

/* thread_exit 的内核实现：不会返回 */
void thread_sys_exit(struct trapframe *tf, int exit_code)
{
  Thread *cur    = &g_threads[g_current_tid];

  /* 保存当前上下文（主要为了调试） */
  cur->tf        = *tf;
  cur->exit_code = exit_code;
  cur->state     = THREAD_ZOMBIE;

  if (cur->join_waiter >= 0) {
    Thread *w = &g_threads[cur->join_waiter];

    /* 如果 join 时传了 status 指针，这里写入 exit_code */
    if (w->join_status_ptr != 0) {
      int *p = (int *)w->join_status_ptr;
      *p     = exit_code;
    }

    /* 设置 join 返回值为 0（成功） */
    w->tf.a0           = 0;

    /* 清理等待关系并唤醒 joiner */
    w->waiting_for     = -1;
    w->join_status_ptr = 0;
    w->state           = THREAD_RUNNABLE;
  }

  cur->join_waiter = -1;

  /* 不在这里回收 slot，留给 join() 做 */
  schedule(tf);
  /* 不会返回到调用 thread_exit() 的那条 C 语句 */
}

/* join 的内核实现：
 *  - target_tid: 要等待的线程
 *  - status_ptr: 用户传入的 int*（可以为 0）
 */
void thread_sys_join(struct trapframe *tf, tid_t target_tid,
                     uintptr_t status_ptr)
{
  Thread *cur = &g_threads[g_current_tid];

  /* 一些基本检查 */
  if (target_tid <= 0 || target_tid >= THREAD_MAX) {
    tf->a0 = -1; /* EINVAL */
    return;
  }
  if (target_tid == g_current_tid) {
    tf->a0 = -2; /* EDEADLK: 自己 join 自己 */
    return;
  }

  Thread *t = &g_threads[target_tid];

  if (t->state == THREAD_UNUSED) {
    tf->a0 = -3; /* ESRCH: 该线程不存在或已被回收 */
    return;
  }

  /* 如果目标已经是 ZOMBIE：立刻回收并返回 */
  if (t->state == THREAD_ZOMBIE) {
    if (status_ptr != 0) {
      int *p = (int *)status_ptr;
      *p     = t->exit_code;
    }
    recycle_thread(target_tid);
    tf->a0 = 0;
    return;
  }

  /* 目标还有别的 joiner？简化起见：一次只允许一个 joiner */
  if (t->join_waiter >= 0 && t->join_waiter != g_current_tid) {
    tf->a0 = -4; /* EBUSY: 已有其它线程在 join 它 */
    return;
  }

  /* 走到这里：目标仍在运行中，需要阻塞当前线程等待 */

  cur->state           = THREAD_WAITING;
  cur->waiting_for     = target_tid;
  cur->join_status_ptr = status_ptr;

  t->join_waiter       = g_current_tid;

  /* 阻塞当前线程，切换到其它线程。
   *
   * 注意：
   *  - 此时不要写 tf->a0（返回值），因为马上要被别的线程的 tf 覆盖。
   *  - 当目标 thread_exit() 时，会在 w->tf.a0 写入 0（成功），
   *    然后把 joiner 的 state 变成 RUNNABLE。
   */
  schedule(tf);
}

/* -------------------------------------------------------------------------- */
/* Introspection                                                              */
/* -------------------------------------------------------------------------- */

tid_t thread_current(void) { return g_current_tid; }

const char *thread_name(tid_t tid)
{
  Thread *t = thread_by_tid(tid);
  if (!t) {
    return "?";
  }
  return t->name;
}
