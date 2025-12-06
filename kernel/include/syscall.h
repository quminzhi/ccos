#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include "thread.h"

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
};

uint64_t write(int fd, const void *buf, uint64_t len);
uint64_t read(int fd, void *buf, uint64_t len);

/* 等待 tid 结束。
 *  - status_out != NULL 时，把目标线程的 exit_code 写进去。
 *  - 返回：
 *      0   : 成功
 *     <0   : 失败（例如 tid 无效 / 已被回收 / 已有其它 joiner 等）
 */
int thread_join(tid_t tid, int *status_out);
tid_t thread_create(thread_entry_t entry, void *arg, const char *name);
void thread_exit(int exit_code);
void thread_sleep(uint64_t ticks);

#endif  // SYSCALL_H
