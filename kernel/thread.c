#include <stdint.h>
#include <stddef.h>

#include "thread.h"
#include "uthread.h"
#include "trap.h"
#include "log.h"
#include "platform.h"
#include "riscv_csr.h"
#include "cpu.h"

extern tid_t g_stdin_waiter;
void *memset(void *s, int c, size_t n); /* string.h */
void arch_first_switch(struct trapframe *tf);

/* -------------------------------------------------------------------------- */
/* Configuration check                                                        */
/* -------------------------------------------------------------------------- */

#if THREAD_MAX <= MAX_HARTS
#error "THREAD_MAX must be at least MAX_HARTS + 1 (for idle + kernel main)"
#endif

#define USER_THREAD 1
#define KERN_THREAD 0

#define FIRST_TID   MAX_HARTS /* [0,MAX_HARTS-1] for idle of each CPU */

/* -------------------------------------------------------------------------- */
/* Types & globals                                                            */
/* -------------------------------------------------------------------------- */

Thread g_threads[THREAD_MAX];
static uint8_t g_thread_stacks[THREAD_MAX][THREAD_STACK_SIZE];

static void idle_main(void *arg) __attribute__((noreturn));

static uint64_t g_ticks = 0;

static void sched_notify_runnable(void) {
  if (!smp_boot_done) return;
  smp_kick_all_others();
}

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

void thread_mark_running(Thread *t, uint32_t hartid) {
  t->running_hart = (int32_t)hartid;

  // Count how many times the thread reaches RUNNING.
  t->runs++;

  // Count migrations only after it has run at least once and hart changes.
  if (t->last_hart >= 0 && t->last_hart != (int32_t)hartid) {
    t->migrations++;
  }
}

void thread_mark_not_running(Thread *t) {
  if (t->running_hart >= 0) {
    t->last_hart = t->running_hart;
  }
  t->running_hart = -1;
}

static inline tid_t current_tid_get(void) {
  return (tid_t)cpu_this()->current_tid;
}

static inline void current_tid_set(tid_t tid) {
  cpu_this()->current_tid = (int)tid;
}

static inline void tf_clear(struct trapframe *tf) {
  memset(tf, 0, sizeof(*tf));
}

static inline Thread *thread_by_tid(tid_t tid) {
  if (tid < 0 || tid >= THREAD_MAX) {
    return NULL;
  }
  return &g_threads[tid];
}

/* Find a free thread slot (start at 1 so tid 0 stays reserved for idle). */
static tid_t alloc_thread_slot(void) {
  for (int i = 1; i < THREAD_MAX; ++i) {
    if (g_threads[i].state == THREAD_UNUSED) {
      return i;
    }
  }
  return -1;
}

static void init_thread_context_s(Thread *t, thread_entry_t entry, void *arg) {
  struct trapframe *tf = &t->tf;

  tf_clear(tf);

  uintptr_t sp = (uintptr_t)(t->stack_base + THREAD_STACK_SIZE);
  sp &= ~(uintptr_t)0xFUL;

  tf->sp   = sp;
  tf->sepc = (uintptr_t)entry;
  tf->a0   = (uintptr_t)arg;

  reg_t s  = csr_read(sstatus);
  s &= ~(SSTATUS_SPP | SSTATUS_SIE);  // Clear mode/interrupt bits first.
  s |= SSTATUS_SPP;                   // SPP=1 so sret returns to S-mode.
  s |= SSTATUS_SPIE;                  // Re-enable S-mode interrupts after sret.
  tf->sstatus = s;
}

static void init_thread_context_u(Thread *t, thread_entry_t entry, void *arg) {
  struct trapframe *tf = &t->tf;
  tf_clear(tf);

  uintptr_t sp = (uintptr_t)(t->stack_base + THREAD_STACK_SIZE);
  sp &= ~(uintptr_t)0xFUL;

  tf->sp   = sp;
  tf->sepc = (uintptr_t)entry;  // User-space PC.
  tf->a0   = (uintptr_t)arg;    // First argument.

  reg_t s  = csr_read(sstatus);
  s &= ~(SSTATUS_SPP | SSTATUS_SIE);
  // SPP=0 so sret returns to U-mode; set SPIE so U-mode can be interrupted.
  s |= SSTATUS_SPIE;
  tf->sstatus = s;
}

/* Idle thread: simple busy loop that calls platform_idle(). */
static void idle_main(void *arg) {
  (void)arg;

  for (;;) {
    platform_idle();
  }
}

/* Recycle a thread that has been joined (return slot to UNUSED). */
static void recycle_thread(tid_t tid) {
  if (tid <= 0 || tid >= THREAD_MAX) {
    return; /* Leave idle/main slots untouched. */
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

  t->running_hart = -1;
  t->last_hart    = -1;
  t->migrations   = 0;
  t->runs         = 0;
  /* The stack array g_thread_stacks[tid] stays allocated for reuse. */
}

static char s_idle_names[MAX_HARTS][16];

static const char *idle_name_for_hart(uint32_t hartid) {
  // Generate "idleX" without snprintf to avoid freestanding issues.
  char *p    = s_idle_names[hartid];
  p[0]       = 'i';
  p[1]       = 'd';
  p[2]       = 'l';
  p[3]       = 'e';

  // Simple decimal conversion.
  uint32_t x = hartid;
  char tmp[10];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + (x % 10));
    x /= 10;
  } while (x && n < (int)sizeof(tmp));

  int k = 4;
  for (int i = n - 1; i >= 0; --i) {
    p[k++] = tmp[i];
  }
  p[k] = '\0';
  return p;
}

static tid_t thread_create_user(thread_entry_t entry, void *arg,
                                const char *name);

static tid_t thread_create_user_main(thread_entry_t user_main, void *arg) {
  tid_t tid = thread_create_user(user_main, arg, "user_main");
  if (tid < 0) {
    pr_err("no slot for user_main\n");
  }
  return tid;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

/* Initialization and configure S-mode idle for each hart and prepare user main
 */
void threads_init(thread_entry_t user_main) {
  g_ticks = 0;

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

    g_threads[i].running_hart     = -1;
    g_threads[i].last_hart        = -1;
    g_threads[i].migrations       = 0;
    g_threads[i].runs             = 0;
    tf_clear(&g_threads[i].tf);
  }

  // prepare idle thread (idle tid = hartid)
  for (uint32_t hid = 0; hid < (uint32_t)MAX_HARTS; ++hid) {
    Thread *idle        = &g_threads[hid];
    idle->state         = THREAD_RUNNABLE;
    idle->name          = idle_name_for_hart(hid);
    idle->stack_base    = g_thread_stacks[hid];
    idle->is_user       = KERN_THREAD;
    idle->can_be_killed = 0;

    init_thread_context_s(idle, idle_main, (void *)(uintptr_t)hid);
  }

  // create thread for user main
  tid_t user_main_tid = thread_create_user_main(user_main, NULL);
  ASSERT(user_main_tid == FIRST_TID);
}

/* Create a new thread: return tid or -1 on failure. */
tid_t thread_create_kern(thread_entry_t entry, void *arg, const char *name) {
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
  sched_notify_runnable();

  return tid;
}

static tid_t thread_create_user(thread_entry_t entry, void *arg,
                                const char *name) {
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
  sched_notify_runnable();

  return tid;
}

/* Called each timer tick to update SLEEPING threads to RUNNABLE. */
void threads_tick(void) {
  g_ticks++;

  int woke_any = 0;
  for (int i = 0; i < THREAD_MAX; ++i) {
    Thread *t = &g_threads[i];
    if (t->state == THREAD_SLEEPING && t->wakeup_tick <= g_ticks) {
      t->wakeup_tick = 0;
      t->state       = THREAD_RUNNABLE;
      woke_any       = 1;
    }
  }

  if (woke_any) {
    sched_notify_runnable();
  }
}

struct trapframe *schedule(struct trapframe *tf) {
  cpu_t *c      = cpu_this();
  tid_t cur_tid = c->current_tid;
  Thread *cur   = &g_threads[cur_tid];

  ASSERT(tf == cpu_this()->cur_tf);

  if (cur->state == THREAD_RUNNING) cur->state = THREAD_RUNNABLE;
  thread_mark_not_running(cur);

  tid_t next_tid = -1;

  for (int off = 1; off < THREAD_MAX; ++off) {
    tid_t cand = (cur_tid + off) % THREAD_MAX;
    if (cand < FIRST_TID) continue;  // Skip idle tids.
    if (g_threads[cand].state == THREAD_RUNNABLE) {
      next_tid = cand;
      break;
    }
  }

  // If currently running a normal thread, keep executing it.
  if (next_tid < 0) {
    if (cur_tid >= FIRST_TID && cur->state == THREAD_RUNNABLE) {
      next_tid = cur_tid;
    } else {
      next_tid = c->idle_tid;
    }
  }

  c->current_tid = next_tid;
  Thread *next   = &g_threads[next_tid];
  next->state    = THREAD_RUNNING;

  if (next_tid != cur_tid) c->ctx_switches++;
  thread_mark_running(next, c->hartid);

  c->cur_tf = &next->tf;
  return c->cur_tf;
}

void thread_block(struct trapframe *tf) {
  tid_t cur_tid = current_tid_get();
  Thread *cur   = &g_threads[cur_tid];

  cur->state    = THREAD_BLOCKED;
  schedule(tf);

  /* Note:
   *  - schedule() never returns to the current thread; its kernel stack
   *    stops here and execution resumes later in user mode.
   *  - Do not place logic after this call.
   */
}

void thread_wake(tid_t tid) {
  if (tid < 0 || tid >= THREAD_MAX) {
    return;
  }
  Thread *t = &g_threads[tid];
  if (t->state == THREAD_BLOCKED) {
    t->state = THREAD_RUNNABLE;
    sched_notify_runnable();
  }
}

/* -------------------------------------------------------------------------- */
/* Sleep / syscalls (kernel side)                                             */
/* -------------------------------------------------------------------------- */

void thread_sys_sleep(struct trapframe *tf, uint64_t ticks) {
  tid_t cur_tid = current_tid_get();
  Thread *cur   = &g_threads[cur_tid];

  if (ticks == 0) {
    /* sleep(0) acts as yield: do not change state but reschedule. */
    schedule(tf);
    return;
  }

  cur->state       = THREAD_SLEEPING;
  cur->wakeup_tick = g_ticks + ticks;

  schedule(tf);
}

void thread_sys_yield(struct trapframe *tf) {
  thread_sys_sleep(tf, 0);
}

void thread_sys_create(struct trapframe *tf, thread_entry_t entry, void *arg,
                       const char *name) {
  tid_t tid = thread_create_user(entry, arg, name);
  tf->a0    = (uintptr_t)tid;  // Return tid to user mode.
}

void thread_sys_exit(struct trapframe *tf, int exit_code) {
  tid_t cur_tid  = current_tid_get();
  Thread *cur    = &g_threads[cur_tid];
  tid_t self_tid = cur_tid;
  tid_t joiner   = cur->join_waiter;

  /* Save the current context (useful for debugging/backtraces). */
  cur->tf        = *tf;
  cur->exit_code = exit_code;
  cur->state     = THREAD_ZOMBIE;

  if (joiner >= 0 && joiner < THREAD_MAX) {
    Thread *w = &g_threads[joiner];

    /* If join provided a status pointer, write exit_code into it. */
    if (w->join_status_ptr != 0) {
      int *p = (int *)w->join_status_ptr;
      *p     = exit_code;
    }

    /* Cause join to return 0 (success). */
    w->tf.a0           = 0;

    /* Clear wait relationships and wake the joiner. */
    w->waiting_for     = -1;
    w->join_status_ptr = 0;
    if (w->state == THREAD_WAITING) {
      w->state = THREAD_RUNNABLE;
    }

    /* If a joiner exists, recycle immediately to avoid lingering ZOMBIE. */
    recycle_thread(self_tid);
  }

  cur->join_waiter = -1;

  /* Threads without joiners remain ZOMBIE until someone joins them. */

  schedule(tf);
}

/* Kernel-side join implementation:
 *  - target_tid: thread being waited on
 *  - status_ptr: optional user int*
 */
void thread_sys_join(struct trapframe *tf, tid_t target_tid,
                     uintptr_t status_ptr) {
  tid_t cur_tid = current_tid_get();
  Thread *cur   = &g_threads[cur_tid];

  /* Basic checks. */
  if (target_tid <= 0 || target_tid >= THREAD_MAX) {
    tf->a0 = -1; /* EINVAL */
    return;
  }
  if (target_tid == cur_tid) {
    tf->a0 = -2; /* EDEADLK: joining self. */
    return;
  }

  Thread *t = &g_threads[target_tid];

  if (t->state == THREAD_UNUSED) {
    tf->a0 = -3; /* ESRCH: thread missing or already recycled. */
    return;
  }

  /* Target already ZOMBIE: recycle immediately and return. */
  if (t->state == THREAD_ZOMBIE) {
    if (status_ptr != 0) {
      int *p = (int *)status_ptr;
      *p     = t->exit_code;
    }
    recycle_thread(target_tid);
    tf->a0 = 0;
    return;
  }

  /* Another joiner already waiting? Allow only one joiner at a time. */
  if (t->join_waiter >= 0 && t->join_waiter != cur_tid) {
    tf->a0 = -4; /* EBUSY: some other thread is joining it. */
    return;
  }

  /* Reaching this point means the target still runs; block current thread. */

  cur->state           = THREAD_WAITING;
  cur->waiting_for     = target_tid;
  cur->join_status_ptr = status_ptr;

  t->join_waiter       = cur_tid;

  /* Block the current thread and switch away.
   *
   * Notes:
   *  - Do not add logic after this point.
   *  - We do not set the return value here; thread_sys_exit()
   *    writes w->tf.a0 = 0 when it wakes the joiner.
   */
  schedule(tf);
}

int thread_sys_list(struct u_thread_info *ubuf, int max) {
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
    dst->cpu                  = t->running_hart;
    dst->last_hart            = t->last_hart;
    dst->migrations           = t->migrations;
    dst->runs                 = t->runs;

    int j = 0;
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

void thread_sys_kill(struct trapframe *tf, tid_t target_tid) {
  tid_t cur_tid = current_tid_get();
  /* Basic checks. */
  if (target_tid < 0 || target_tid >= THREAD_MAX) {
    tf->a0 = -1;  // EINVAL
    return;
  }

  if (target_tid == 0) {
    tf->a0 = -2;  // Killing idle threads is not allowed.
    return;
  }

  if (target_tid == cur_tid) {
    tf->a0 = -4;  // Do not allow kill-based suicide; use thread_exit instead.
    return;
  }

  Thread *t = &g_threads[target_tid];

  if (t->can_be_killed == 0) {
    tf->a0 = -3;  // Thread may not be killed.
    return;
  }

  if (t->state == THREAD_UNUSED) {
    tf->a0 = -3;  // ESRCH: thread not found.
    return;
  }

  if (t->state == THREAD_ZOMBIE) {
    tf->a0 = 0;  // Already dead; treat as success.
    return;
  }

  /* Remember whether a joiner exists. */
  tid_t joiner = t->join_waiter;

  /* Force ZOMBIE state (SIGKILL semantics). */
  t->exit_code = THREAD_EXITCODE_SIGKILL;  // Usually -9.
  t->state     = THREAD_ZOMBIE;

  /* If there is a joiner, reuse the normal exit logic for it. */
  if (joiner >= 0 && joiner < THREAD_MAX) {
    Thread *w = &g_threads[joiner];

    if (w->join_status_ptr != 0) {
      int *p = (int *)w->join_status_ptr;
      *p     = t->exit_code;
    }

    /* Make join return 0 (success). */
    w->tf.a0           = 0;

    /* Clear wait state and wake the joiner. */
    w->waiting_for     = -1;
    w->join_status_ptr = 0;
    if (w->state == THREAD_WAITING) {
      w->state = THREAD_RUNNABLE;
    }

    /* Immediately recycle the killed thread's slot when a joiner exists. */
    recycle_thread(target_tid);
  }

  t->join_waiter = -1;

  /* kill syscall returns 0 to indicate success. */
  tf->a0         = 0;
}

/* -------------------------------------------------------------------------- */
/* Introspection                                                              */
/* -------------------------------------------------------------------------- */

tid_t thread_current(void) {
  return current_tid_get();
}

const char *thread_name(tid_t tid) {
  Thread *t = thread_by_tid(tid);
  if (!t) {
    return "?";
  }
  return t->name;
}

void print_thread_prefix(void) {
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

void thread_wait_for_stdin(char *buf, uint64_t len, struct trapframe *tf) {
  /* No data: record read context and block the thread. */
  Thread *cur           = &g_threads[thread_current()];

  cur->pending_read_buf = (uintptr_t)buf;
  cur->pending_read_len = len;

  /* Do not return to this thread; suspend it instead. */
  g_stdin_waiter        = cur->id;
  thread_block(tf);
}

void thread_read_from_stdin(console_reader_t read) {
  Thread *t = &g_threads[g_stdin_waiter];

  if (t->pending_read_buf == 0 || t->pending_read_len == 0) {
    thread_wake(g_stdin_waiter);
    return;
  }

  char *user_buf = (char *)t->pending_read_buf;
  size_t max_len = (size_t)t->pending_read_len;

  int n          = read(user_buf, max_len);
  if (n <= 0) {
    // If nothing was read (rare race), wait for the next interrupt.
    return;
  }

  /* Program the read() return value so user space sees n next run. */
  t->tf.a0            = (uintptr_t)n;

  /* Clear the pending_read context. */
  t->pending_read_buf = 0;
  t->pending_read_len = 0;

  /* 4. Wake the thread and clear the waiter flag. */
  thread_wake(g_stdin_waiter);
}
