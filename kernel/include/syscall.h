#ifndef SYSCALL_H
#define SYSCALL_H

// user side

#include <stdint.h>
#include <stddef.h>
#include "thread_sys.h"
#include "uapi.h"

struct timespec;

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

enum {
  SYS_SLEEP         = 1,
  SYS_THREAD_EXIT   = 2,
  SYS_THREAD_JOIN   = 3,
  SYS_THREAD_CREATE = 4,
  SYS_WRITE         = 5,
  SYS_READ          = 6,
  SYS_THREAD_LIST   = 7,
  SYS_THREAD_KILL   = 8,
  SYS_CLOCK_GETTIME = 9,
  SYS_IRQ_GET_STATS = 10
};

uint64_t write(int fd, const void *buf, uint64_t len);
uint64_t read(int fd, void *buf, uint64_t len);
void sleep(uint64_t ticks);

/* 等待 tid 结束。
 *  - status_out != NULL 时，把目标线程的 exit_code 写进去。
 *  - 返回：
 *      0   : 成功
 *     <0   : 失败（例如 tid 无效 / 已被回收 / 已有其它 joiner 等）
 */
int thread_join(tid_t tid, int *status_out);
tid_t thread_create(thread_entry_t entry, void *arg, const char *name);
void thread_exit(int exit_code) __attribute__((noreturn));

int thread_list(struct u_thread_info *buf, int max);  // 返回实际个数 / 负错误码
int thread_kill(tid_t tid);                           // 0=OK, <0=错误

// clock_id support CLOCK_REALTIME(0) only
int clock_gettime(int clock_id, struct timespec *ts);

long irq_get_stats(struct irqstat_user *ubuf, size_t n);

#endif  // SYSCALL_H
