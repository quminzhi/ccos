#ifndef SYSFILE_H
#define SYSFILE_H

#include <stdint.h>

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

uint64_t sys_write(int fd, const char *buf, uint64_t len);
uint64_t sys_read(int fd, char *buf, uint64_t len);

#endif  // SYSFILE_H
