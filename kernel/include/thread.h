#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include "trap.h"

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

#define DELTA_TICKS 10000000UL /* ~1s */

/* -------------------------------------------------------------------------- */
/* Public types                                                               */
/* -------------------------------------------------------------------------- */

typedef int tid_t; /* 线程 ID = g_threads[] 的 index      */
typedef void (*thread_entry_t)(void *arg) __attribute__((noreturn));

/* -------------------------------------------------------------------------- */
/* Core thread API                                                            */
/* -------------------------------------------------------------------------- */

/* 初始化线程子系统：
 *  - tid 0: idle 线程
 *  - tid 1: main 线程（当前正在运行）
 */
void threads_init(void);

tid_t thread_create_kern(thread_entry_t entry, void *arg, const char *name);

/* start first user main */
tid_t threads_exec(thread_entry_t entry, void *arg);

/* 每个 timer tick 调用一次（通常在定时器中断里） */
void threads_tick(void);

/* 核心调度函数：在 trap 中切换当前线程 */
void schedule(struct trapframe *tf);

/* -------------------------------------------------------------------------- */
/* Sleeping / syscalls                                                        */
/* -------------------------------------------------------------------------- */

/* trap_handler 用：
 *  - 处理 SYS_SLEEP：把当前线程标记为 SLEEPING，并 schedule。
 */
void thread_sys_sleep(struct trapframe *tf, uint64_t ticks);

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

/* -------------------------------------------------------------------------- */
/* Introspection / utils                                                      */
/* -------------------------------------------------------------------------- */

/* 当前线程 tid */
tid_t thread_current(void);

/* tid 对应的名字（用于调试） */
const char *thread_name(tid_t tid);

void print_thread_prefix(void);

#endif /* THREAD_H */
