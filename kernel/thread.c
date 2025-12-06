#include <stdint.h>
#include <stddef.h>

#include "thread.h"
#include "thread_sys.h"
#include "trap.h"
#include "log.h"
#include "platform.h"
#include "riscv_csr.h"

extern tid_t g_stdin_waiter;
void *memset(void *s, int c, size_t n); /* klib.h */
void arch_first_switch(struct trapframe *tf);

/* -------------------------------------------------------------------------- */
/* Configuration check                                                        */
/* -------------------------------------------------------------------------- */

#if THREAD_MAX < 2
#error "THREAD_MAX must be at least 2 (for idle + main)"
#endif

#define USER_THREAD 1
#define KERN_THREAD 0

/* -------------------------------------------------------------------------- */
/* Types & globals                                                            */
/* -------------------------------------------------------------------------- */

typedef struct Thread {
  tid_t id;
  ThreadState state;
  uint64_t wakeup_tick; /* SLEEPING 时的唤醒 tick（绝对时间） */
  const char *name;
  int is_user; /* 0 = S 模式线程; 1 = U 模式线程（可选字段）*/
  int can_be_killed;

  struct trapframe tf; /* 保存的寄存器上下文 */

  uint8_t *stack_base; /* 栈底（main 用 boot 栈 -> NULL） */

  /* exit / join 相关 */
  int exit_code;             /* thread_exit(exit_code) 保存的值 */
  tid_t join_waiter;         /* 有谁在 join 我？（-1 表示没有） */
  tid_t waiting_for;         /* 我在 join 谁？（仅 WAITING 时有用） */
  uintptr_t join_status_ptr; /* join 时传入的 int*，保存 exit_code 用 */

  /* 用于阻塞式 read 的上下文（最小版本：只支持一个 read 请求） */
  uintptr_t pending_read_buf; /* 用户传来的 buf 指针 */
  uint64_t pending_read_len;  /* 用户传来的 len      */
} Thread;

static Thread g_threads[THREAD_MAX];
static uint8_t g_thread_stacks[THREAD_MAX][THREAD_STACK_SIZE];

static tid_t g_current_tid = 0;
static uint64_t g_ticks    = 0;

static void idle_main(void *arg) __attribute__((noreturn));

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

/* 找空闲 slot（1 开始：0 idle 保留） */
static tid_t alloc_thread_slot(void)
{
  for (int i = 1; i < THREAD_MAX; ++i) {
    if (g_threads[i].state == THREAD_UNUSED) {
      return i;
    }
  }
  return -1;
}

static void init_thread_context_s(Thread *t, thread_entry_t entry, void *arg)
{
  struct trapframe *tf = &t->tf;

  tf_clear(tf);

  uintptr_t sp = (uintptr_t)(t->stack_base + THREAD_STACK_SIZE);
  sp &= ~(uintptr_t)0xFUL;

  tf->sp   = sp;
  tf->sepc = (uintptr_t)entry;
  tf->a0   = (uintptr_t)arg;

  reg_t s  = csr_read(sstatus);
  s &= ~(SSTATUS_SPP | SSTATUS_SIE);  // 先清 mode + 中断
  s |= SSTATUS_SPP;                   // SPP=1 -> sret 到 S 模式
  s |= SSTATUS_SPIE;                  // sret 之后开 S 模式中断
  tf->sstatus = s;
}

static void init_thread_context_u(Thread *t, thread_entry_t entry, void *arg)
{
  struct trapframe *tf = &t->tf;
  tf_clear(tf);

  uintptr_t sp = (uintptr_t)(t->stack_base + THREAD_STACK_SIZE);
  sp &= ~(uintptr_t)0xFUL;

  tf->sp   = sp;
  tf->sepc = (uintptr_t)entry;  // user-space PC
  tf->a0   = (uintptr_t)arg;    // 第一个参数

  reg_t s  = csr_read(sstatus);
  s &= ~(SSTATUS_SPP | SSTATUS_SIE);
  // SPP=0 -> sret 到 U 模式
  // 同时把 SPIE=1，这样 sret 之后 U 模式可以被 S-mode interrupt 打断
  s |= SSTATUS_SPIE;
  tf->sstatus = s;
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
    g_threads[i].id               = i;
    g_threads[i].state            = THREAD_UNUSED;
    g_threads[i].wakeup_tick      = 0;
    g_threads[i].name             = "unused";
    g_threads[i].stack_base       = NULL;
    g_threads[i].can_be_killed    = 0;
    g_threads[i].exit_code        = 0;
    g_threads[i].join_waiter      = -1;
    g_threads[i].waiting_for      = -1;
    g_threads[i].join_status_ptr  = 0;
    g_threads[i].pending_read_buf = 0;
    g_threads[i].pending_read_len = 0;
    tf_clear(&g_threads[i].tf);
  }

  /* tid 0: idle (S-mode) */
  Thread *idle     = &g_threads[0];
  idle->state      = THREAD_RUNNABLE;
  idle->name       = "idle";
  idle->stack_base = g_thread_stacks[0];
  init_thread_context_s(idle, idle_main, NULL);
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
  t->is_user         = KERN_THREAD;

  init_thread_context_s(t, entry, arg);

  return tid;
}

static tid_t thread_create_user(thread_entry_t entry, void *arg,
                                const char *name)
{
  tid_t tid = alloc_thread_slot();

  if (tid < 0) {
    return -1;
  }

  Thread *t        = &g_threads[tid];
  t->state         = THREAD_RUNNABLE;
  t->wakeup_tick   = 0;
  t->name          = name ? name : "uthread";
  t->stack_base    = g_thread_stacks[tid];
  t->is_user       = USER_THREAD;
  t->can_be_killed = 1;

  init_thread_context_u(t, entry, arg);

  return tid;
}

// "exec"
tid_t threads_exec(thread_entry_t user_main, void *arg)
{
  tid_t tid = thread_create_user(user_main, arg, "user_main");
  if (tid < 0) {
    pr_err("no slot for user_main\n");
  }

  g_current_tid    = tid;
  Thread *t        = &g_threads[tid];
  t->can_be_killed = 0;
  arch_first_switch(&t->tf);  // 不会返回

  for (;;) {
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

void thread_block(struct trapframe *tf)
{
  Thread *cur = &g_threads[g_current_tid];

  cur->state  = THREAD_BLOCKED;
  schedule(tf);

  /* 注意：
   *  - 对当前线程而言，这个 schedule() 不会“回来”继续执行，
   *    它的内核调用栈就此终止，等以后再被调度时，会直接从 user 慢慢往前跑。
   *  - 也就是说，不要在这里后面再写逻辑。
   */
}

void thread_wake(tid_t tid)
{
  if (tid < 0 || tid >= THREAD_MAX) {
    return;
  }
  Thread *t = &g_threads[tid];
  if (t->state == THREAD_BLOCKED) {
    t->state = THREAD_RUNNABLE;
  }
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
  tid_t tid = thread_create_user(entry, arg, name);
  tf->a0    = (uintptr_t)tid;  // 返回 tid 给用户态
}

void thread_sys_exit(struct trapframe *tf, int exit_code)
{
  Thread *cur    = &g_threads[g_current_tid];
  tid_t self_tid = g_current_tid;
  tid_t joiner   = cur->join_waiter;

  /* 保存当前上下文（主要为了调试 / backtrace） */
  cur->tf        = *tf;
  cur->exit_code = exit_code;
  cur->state     = THREAD_ZOMBIE;

  if (joiner >= 0 && joiner < THREAD_MAX) {
    Thread *w = &g_threads[joiner];

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
    if (w->state == THREAD_WAITING) {
      w->state = THREAD_RUNNABLE;
    }

    /* 有 joiner 的话，直接在这里回收自己，避免长期 ZOMBIE */
    recycle_thread(self_tid);
  }

  cur->join_waiter = -1;

  /* 注意：没有 joiner 的线程保持 ZOMBIE 状态，等将来别的线程 join 它。 */

  schedule(tf);
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
   *  - 这里之后不要再写任何逻辑了！
   *  - 我们不在这里设置返回值；返回值由 thread_sys_exit()
   *    在唤醒 joiner 的时候，通过修改 w->tf.a0=0 来完成。
   */
  schedule(tf);
}

int thread_sys_list(struct u_thread_info *ubuf, int max)
{
  if (!ubuf || max <= 0) {
    return -1;  // EINVAL
  }
  int count = 0;
  for (int i = 0; i < THREAD_MAX && count < max; ++i) {
    Thread *t = &g_threads[i];
    if (t->state == THREAD_UNUSED) {
      continue;
    }
    struct u_thread_info *dst = &ubuf[count];
    dst->tid                  = t->id;
    dst->state                = (int)t->state;
    dst->is_user              = t->is_user ? 1 : 0;
    dst->exit_code            = t->exit_code;

    int j                     = 0;
    if (t->name) {
      while (t->name[j] && j < (int)sizeof(dst->name) - 1) {
        dst->name[j] = t->name[j];
        ++j;
      }
    }
    dst->name[j] = '\0';
    ++count;
  }
  return count;
}

void thread_sys_kill(struct trapframe *tf, tid_t target_tid)
{
  /* 基本检查 */
  if (target_tid < 0 || target_tid >= THREAD_MAX) {
    tf->a0 = -1;  // EINVAL
    return;
  }

  if (target_tid == 0) {
    tf->a0 = -2;  // 不允许杀 idle
    return;
  }

  if (target_tid == g_current_tid) {
    tf->a0 = -4;  // 不允许通过 kill 自杀（让用户态用 thread_exit）
    return;
  }

  Thread *t = &g_threads[target_tid];

  if (t->can_be_killed == 0) {
    tf->a0 = -3;  // 该线程不许 killed
    return;
  }

  if (t->state == THREAD_UNUSED) {
    tf->a0 = -3;  // ESRCH: 不存在
    return;
  }

  if (t->state == THREAD_ZOMBIE) {
    tf->a0 = 0;  // 已经死了，当成功
    return;
  }

  /* 记录一下是否有 joiner */
  tid_t joiner = t->join_waiter;

  /* 强制标记为 ZOMBIE（SIGKILL 风格） */
  t->exit_code = THREAD_EXITCODE_SIGKILL;  // 一般是 -9
  t->state     = THREAD_ZOMBIE;

  /* 如果有人在 join 它，就按正常 exit 的逻辑处理 joiner */
  if (joiner >= 0 && joiner < THREAD_MAX) {
    Thread *w = &g_threads[joiner];

    if (w->join_status_ptr != 0) {
      int *p = (int *)w->join_status_ptr;
      *p     = t->exit_code;
    }

    /* 设置 join 返回值为 0（成功） */
    w->tf.a0           = 0;

    /* 清理等待关系并唤醒 joiner */
    w->waiting_for     = -1;
    w->join_status_ptr = 0;
    if (w->state == THREAD_WAITING) {
      w->state = THREAD_RUNNABLE;
    }

    /* 有 joiner 的话，立刻回收被 kill 的线程 slot，避免长期 ZOMBIE */
    recycle_thread(target_tid);
  }

  t->join_waiter = -1;

  /* kill syscall 本身的返回值：0 = 成功 */
  tf->a0         = 0;
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

void print_thread_prefix(void)
{
  tid_t tid        = thread_current();
  const char *name = thread_name(tid);
  char mode        = g_threads[tid].is_user ? 'U' : 'S';

  platform_putc('[');
  platform_put_hex64((uintptr_t)tid);
  platform_putc(':');
  platform_puts(name);
  platform_putc(':');
  platform_putc(mode);
  platform_puts("] ");
}

void thread_wait_for_stdin(char *buf, uint64_t len, struct trapframe *tf)
{
  /* 没数据：登记 read 上下文，并 block 当前线程 */
  Thread *cur           = &g_threads[thread_current()];

  cur->pending_read_buf = (uintptr_t)buf;
  cur->pending_read_len = len;

  /* 这里不返回给当前线程，而是把它挂起 */
  g_stdin_waiter        = cur->id;
  thread_block(tf);
}

void thread_read_from_stdin(console_reader_t read)
{
  Thread *t = &g_threads[g_stdin_waiter];

  if (t->pending_read_buf == 0 || t->pending_read_len == 0) {
    thread_wake(g_stdin_waiter);
    return;
  }

  char *user_buf = (char *)t->pending_read_buf;
  size_t max_len = (size_t)t->pending_read_len;

  int n          = read(user_buf, max_len);
  if (n <= 0) {
    // 没读到东西（极端 race），那就等下次中断再说
    return;
  }

  /* 设置 read 的返回值：下次这线程被调度、trap 返回时，用户看到的是 n */
  t->tf.a0            = (uintptr_t)n;

  /* 清理 pending_read 上下文 */
  t->pending_read_buf = 0;
  t->pending_read_len = 0;

  /* 4. 唤醒线程，并把 waiter 标记清掉 */
  thread_wake(g_stdin_waiter);
}
