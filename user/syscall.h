#ifndef SYSCALL_H
#define SYSCALL_H

// user side
#include <stdint.h>
#include <stddef.h>
#include "uthread.h"
#include "uapi.h"

struct timespec;

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

uint64_t write(int fd, const void *buf, uint64_t len);
uint64_t read(int fd, void *buf, uint64_t len);
void sleep(uint64_t ticks);

int thread_join(tid_t tid, int *status_out);
tid_t thread_create(thread_entry_t entry, void *arg, const char *name);
void thread_exit(int exit_code) __attribute__((noreturn));

int thread_list(struct u_thread_info *buf, int max);  // Count returned or <0 on error.
int thread_kill(tid_t tid);                           // 0 on success, <0 on error.

// clock_id support CLOCK_REALTIME(0) only
int clock_gettime(int clock_id, struct timespec *ts);

long irq_get_stats(struct irqstat_user *ubuf, size_t n);
int get_hartid(void);
void yield(void);

#endif  // SYSCALL_H
