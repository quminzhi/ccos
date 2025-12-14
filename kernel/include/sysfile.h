#ifndef SYSFILE_H
#define SYSFILE_H

#include <stddef.h>
#include <stdint.h>

#include "uapi.h"

struct timespec;

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

struct trapframe;

uint64_t sys_write(int fd, const char *buf, uint64_t len);
uint64_t sys_read(int fd, char *buf, uint64_t len, struct trapframe *tf,
                  int *is_non_block_read);
long sys_clock_gettime(int clock_id, struct timespec *u_ts);
long sys_irq_get_stats(struct irqstat_user *ubuf, size_t n);

#endif  /* SYSFILE_H */
