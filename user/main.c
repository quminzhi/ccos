#include "thread.h"
#include "platform.h"
#include "syscall.h"

static void worker_thread(void *arg)
{
  int n = (int)(uintptr_t)arg;

  print_thread_prefix();
  platform_puts("worker_thread started, doing some work...\n");

  /* 模拟“工作一段时间” */
  for (int i = 0; i < n; ++i) {
    print_thread_prefix();
    platform_puts("worker_thread step\n");
    thread_sleep(5); /* sleep 一下，给调度器机会切到别的线程 */
  }

  print_thread_prefix();
  platform_puts("worker_thread finished, calling thread_exit\n");

  /* 把 n 当作 exit_code 传回去，方便主线程在 join 后查看 */
  thread_exit(n);

  /* 不会到达这里 */
}

void user_main(void *arg)
{
  (void)arg;

  print_thread_prefix();
  platform_puts("system init done (user).\n");

  /* 创建一个 worker 线程，演示 thread_exit / thread_join */
  tid_t worker_tid =
      thread_create(worker_thread, (void *)(uintptr_t)3, "worker");
  if (worker_tid < 0) {
    platform_puts("Failed to create worker_thread\n");
  }

  /* 如果 worker 创建成功，主线程 join 一次，演示 join/exit 正常工作 */
  if (worker_tid >= 0) {
    int status = 0;

    print_thread_prefix();
    platform_puts("main: joining worker...\n");

    int rc = thread_join(worker_tid, &status);

    print_thread_prefix();
    platform_puts("main: thread_join (worker) returned rc=");
    platform_put_hex64((uintptr_t)rc);
    platform_puts(", exit_code=");
    platform_put_hex64((uintptr_t)status);
    platform_puts("\n");
  }

  for (;;) {
    print_thread_prefix();
    platform_puts("main loop running...\n");
    thread_sleep(10); /* 这里的 ecall 就是 U-mode ECALL，能进 trap.c 了 */
  }
}
