#include "syscall.h"

uint64_t write(int fd, const void *buf, uint64_t len)
{
  register uintptr_t a0 asm("a0") = SYS_WRITE;
  register uintptr_t a1 asm("a1") = (uintptr_t)fd;
  register uintptr_t a2 asm("a2") = (uintptr_t)buf;
  register uintptr_t a3 asm("a3") = (uintptr_t)len;

  __asm__ volatile("ecall"
                   : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3)
                   :
                   : "memory");

  return (int)a0; /* 返回写入的字节数或负数错误码 */
}

uint64_t read(int fd, void *buf, uint64_t len)
{
  register uintptr_t a0 asm("a0") = SYS_READ;
  register uintptr_t a1 asm("a1") = (uintptr_t)fd;
  register uintptr_t a2 asm("a2") = (uintptr_t)buf;
  register uintptr_t a3 asm("a3") = (uintptr_t)len;

  __asm__ volatile("ecall"
                   : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3)
                   :
                   : "memory");

  return (int)a0; /* 返回读到的字节数或负数错误码 */
}

void sleep(uint64_t ticks)
{
  register uintptr_t a0 asm("a0") = SYS_SLEEP;
  register uintptr_t a1 asm("a1") = (uintptr_t)ticks;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : : "memory");
}

/* 用户侧 thread_exit：不会返回 */
void thread_exit(int exit_code)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_EXIT;
  register uintptr_t a1 asm("a1") = (uintptr_t)exit_code;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : : "memory");

  __builtin_unreachable();
}

/* 用户侧 thread_join：阻塞直到 tid 退出 */
int thread_join(tid_t tid, int *status_out)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_JOIN;
  register uintptr_t a1 asm("a1") = (uintptr_t)tid;
  register uintptr_t a2 asm("a2") = (uintptr_t)status_out;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1), "+r"(a2) : : "memory");

  /* 返回值在 a0 里 */
  return (int)a0;
}

tid_t thread_create(thread_entry_t entry, void *arg, const char *name)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_CREATE;
  register uintptr_t a1 asm("a1") = (uintptr_t)entry;
  register uintptr_t a2 asm("a2") = (uintptr_t)arg;
  register uintptr_t a3 asm("a3") = (uintptr_t)name;

  __asm__ volatile("ecall"
                   : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3)
                   :
                   : "memory");

  return (tid_t)a0;  // 返回 tid（<0 表示失败）
}
