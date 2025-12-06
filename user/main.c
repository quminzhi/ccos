#include "thread.h"
#include "syscall.h"
#include "log.h"
#include "ulib.h"
#include "shell.h"

void user_main(void *arg) __attribute__((noreturn));

void assert_test(int *ptr, size_t len)
{
  ASSERT(ptr != NULL);
  ASSERTF(len < 128, "len=%u", (unsigned)len);

  unsigned addr = 0;
  if (addr == 0) {
    PANICF("flash write failed, addr=0x%08x", (unsigned)addr);
  }

  BREAK_IF(addr == 0);  // 只打断点，不挂机
}

void user_main(void *arg)
{
  (void)arg;

  u_puts("Welcome, hacker!");

  for (;;) {
    tid_t shell_tid = shell_start();
    if (shell_tid < 0) {
      u_puts("failed to start shell, retrying after a while...");
      sleep(1);
      continue;
    }

    int status = 0;
    u_printf("main: started shell tid=%d, waiting for it to exit...\n",
             (int)shell_tid);

    thread_join(shell_tid, &status);

    u_printf("main: shell exited, status=%d, restarting a new shell...\n",
             status);
  }

  // 理论上不会走到这里
  thread_exit(0);
}
