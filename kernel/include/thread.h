#ifndef THREAD_H
#define THREAD_H

#include <stddef.h>
#include <stdint.h>

#include "trap.h"
#include "uthread.h"

/* -------------------------------------------------------------------------- */
/* Config                                                                     */
/* -------------------------------------------------------------------------- */

/* 最大线程数（包含 idle + main） */
#ifndef THREAD_MAX
#define THREAD_MAX 8
#endif

/* 每个线程的栈大小（字节） */
#ifndef THREAD_STACK_SIZE
#define THREAD_STACK_SIZE 4096
#endif

#define DELTA_TICKS 100000UL /* ~10ms at 10MHz timebase */

/* Alternative: #define DELTA_TICKS 10000000UL   (~1s) */

/* Basic per-thread state. Keep this struct compact and self-explanatory.
 * Invariants:
 *   - RUNNABLE 当且仅当 on_rq == 1。
 *   - RUNNING 表示当前持有 CPU，on_rq == 0。
 *   - idle 线程永不入 rq。
 */
typedef struct Thread {
  tid_t        id;
  ThreadState  state;
  uint64_t wakeup_tick; /* SLEEPING 时的唤醒 tick（绝对时间） */
  const char *name;
  int is_user; /* 0 = S 模式线程; 1 = U 模式线程（可选字段）*/
  int can_be_killed;
  int detached; /* 1 = detached, auto-recycle on exit/kill */

  /* --- SMP debug/metrics --- */
  int32_t  running_hart;   /* -1 not running; >=0 running on that hart */
  int32_t  last_hart;      /* -1 never ran;  >=0 last hart it ran on */
  uint32_t migrations;     /* number of migrations */
  uint64_t runs;           /* number of times scheduled RUNNING */

  /* runqueue metadata */
  tid_t    rq_next;    /* 单向链表 next */
  uint8_t  on_rq;      /* 是否在某个 runqueue 上 */

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

/* -------------------------------------------------------------------------- */
/* Core thread API                                                            */
/* -------------------------------------------------------------------------- */

extern Thread g_threads[THREAD_MAX];

/* 初始化线程子系统：
 *  - tid 0: idle 线程
 *  - tid 1: main 线程（当前正在运行）
 */
void threads_init(thread_entry_t user_main);
void cpu_enter_idle(uint32_t hartid) __attribute__((noreturn));

tid_t thread_create_kern(thread_entry_t entry, void *arg, const char *name);

void threads_tick(void);

struct trapframe *schedule(struct trapframe *tf);

void thread_block(struct trapframe *tf);
void thread_wake(tid_t tid);

/* -------------------------------------------------------------------------- */
/* Sleeping / syscalls                                                        */
/* -------------------------------------------------------------------------- */

/* trap_handler 用：
 *  - 处理 SYS_SLEEP：把当前线程标记为 SLEEPING，并 schedule。
 */
void thread_sys_sleep(struct trapframe *tf, uint64_t ticks);
void thread_sys_yield(struct trapframe *tf);

/* trap_handler 用：
 *  - 处理 SYS_THREAD_EXIT：把当前线程标记为 ZOMBIE，唤醒 joiner。
 *  - 不会返回到调用 thread_exit() 的那条 C 语句。
 * thread_sys_exit 不能 标成 noreturn。
 * 它逻辑上不会回到用户态的那条语句，但在 C 语义里“会从函数返回”，所以不能对
 * 编译器说它是 noreturn。
 */
void thread_sys_exit(struct trapframe *tf, int exit_code);

/* trap_handler 用：
 *  - 处理 SYS_THREAD_JOIN：
 *      tf->a1 = target_tid
 *      tf->a2 = (uintptr_t)status_ptr
 *  - 如果立刻 join 成功：在 tf->a0 写入返回值（0 或负数）然后直接返回。
 *  - 如果需要等待：把当前线程挂到 target 上，schedule()；等目标 exit 时被唤醒。
 */
void thread_sys_join(struct trapframe *tf, tid_t target_tid,
                     uintptr_t status_ptr);

void thread_sys_create(struct trapframe *tf, thread_entry_t entry, void *arg,
                       const char *name);
int thread_sys_list(struct u_thread_info *ubuf, int max);
void thread_sys_kill(struct trapframe *tf, tid_t target_tid);
/* Mark thread as detached; detached threads auto-recycle, cannot be joined. */
void thread_sys_detach(struct trapframe *tf, tid_t target_tid);
long sys_runqueue_snapshot(struct rq_state *ubuf, size_t n);

/* -------------------------------------------------------------------------- */
/* Introspection / utils                                                      */
/* -------------------------------------------------------------------------- */

/* 当前线程 tid */
tid_t thread_current(void);

/* tid 对应的名字（用于调试） */
const char *thread_name(tid_t tid);

void print_thread_prefix(void);

typedef int (*console_reader_t)(char *buf, size_t len);
void thread_wait_for_stdin(char *buf, uint64_t len, struct trapframe *tf);
void thread_read_from_stdin(console_reader_t reader);

void thread_mark_running(Thread *t, uint32_t hartid);
void thread_mark_not_running(Thread *t);

/* 把 tid 变为 RUNNABLE 并放入目标 hart 的 runqueue；内部会决定是否发 IPI。 */
void thread_make_runnable(tid_t tid, uint32_t preferred_hart);

#endif /* THREAD_H */
