#include "syscall.h"
#include "usyscall.h"

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

  return (int)a0; /* Bytes written or negative error. */
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

  return (int)a0; /* Bytes read or negative error. */
}

void sleep(uint64_t ticks)
{
  register uintptr_t a0 asm("a0") = SYS_SLEEP;
  register uintptr_t a1 asm("a1") = (uintptr_t)ticks;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : : "memory");
}

/* User-side thread_exit: never returns. */
void thread_exit(int exit_code)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_EXIT;
  register uintptr_t a1 asm("a1") = (uintptr_t)exit_code;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : : "memory");

  __builtin_unreachable();
}

/* User-side thread_join: block until tid exits. */
int thread_join(tid_t tid, int *status_out)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_JOIN;
  register uintptr_t a1 asm("a1") = (uintptr_t)tid;
  register uintptr_t a2 asm("a2") = (uintptr_t)status_out;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1), "+r"(a2) : : "memory");

  /* Return value is in a0. */
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

  return (tid_t)a0;  /* tid on success, <0 on failure. */
}

int thread_list(struct u_thread_info *buf, int max)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_LIST;
  register uintptr_t a1 asm("a1") = (uintptr_t)buf;
  register uintptr_t a2 asm("a2") = (uintptr_t)max;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1), "+r"(a2) : : "memory");
  return (int)a0;
}

int thread_kill(tid_t tid)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_KILL;
  register uintptr_t a1 asm("a1") = (uintptr_t)tid;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : : "memory");
  return (int)a0;
}

int thread_detach(tid_t tid)
{
  register uintptr_t a0 asm("a0") = SYS_THREAD_DETACH;
  register uintptr_t a1 asm("a1") = (uintptr_t)tid;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : : "memory");
  return (int)a0;
}

int clock_gettime(int clock_id, struct timespec *ts)
{
  register uintptr_t a0 asm("a0") = SYS_CLOCK_GETTIME;  // syscall number
  register uintptr_t a1 asm("a1") = (uintptr_t)clock_id;
  register uintptr_t a2 asm("a2") = (uintptr_t)ts;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1), "+r"(a2) : : "memory");

  /* Convention: a0 == 0 success, <0 error code. */
  return (int)a0;
}

long irq_get_stats(struct irqstat_user *buf, size_t n)
{
  register uintptr_t a0 asm("a0") = SYS_IRQ_GET_STATS;
  register uintptr_t a1 asm("a1") = (uintptr_t)buf;
  register uintptr_t a2 asm("a2") = (uintptr_t)n;

  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1), "+r"(a2) : : "memory");

  return (long)a0;  /* Entries written, or <0 on error. */
}

int get_hartid(void) {
  register long a0 asm("a0") = SYS_GET_HARTID;
  asm volatile("ecall" : "+r"(a0) : : "memory");
  return (int)a0;
}

void yield() {
  register long a0 asm("a0") = SYS_YIELD;
  asm volatile("ecall" : "+r"(a0) : : "memory");
}
