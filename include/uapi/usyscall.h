#ifndef SYSCALL_NO_H
#define SYSCALL_NO_H

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
  SYS_IRQ_GET_STATS = 10,
  SYS_GET_HARTID    = 11,
  SYS_YIELD         = 12
};

#endif // SYSCALL_NO_H
