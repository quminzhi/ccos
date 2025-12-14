#include "log.h"
#include "shell.h"
#include "syscall.h"
#include "thread.h"
#include "ulib.h"

void user_main(void *arg) __attribute__((noreturn));

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

  /* Should be unreachable. */
  thread_exit(0);
}
