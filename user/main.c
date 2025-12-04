#include "thread.h"
#include "platform.h"
#include "syscall.h"
#include "log.h"

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


static void worker_thread(void *arg)
{
  int n = (int)(uintptr_t)arg;

  print_thread_prefix();
  pr_info("worker_thread started, doing some work...");

  /* 模拟“工作一段时间” */
  for (int i = 0; i < n; ++i) {
    print_thread_prefix();
    pr_info("worker_thread step");
    thread_sleep(1); /* sleep 一下，给调度器机会切到别的线程 */
  }

  print_thread_prefix();
  pr_info("worker_thread finished, calling thread_exit");

  /* 把 n 当作 exit_code 传回去，方便主线程在 join 后查看 */
  thread_exit(n);

  /* 不会到达这里 */
}

void user_main(void *arg)
{
  (void)arg;

  print_thread_prefix();
  pr_info("system init done.");

  // assert_test(NULL, 0);

  /* 创建一个 worker 线程，演示 thread_exit / thread_join */
  tid_t worker_tid =
      thread_create(worker_thread, (void *)(uintptr_t)3, "worker");
  if (worker_tid < 0) {
    pr_err("Failed to create worker_thread");
  }

  /* 如果 worker 创建成功，主线程 join 一次，演示 join/exit 正常工作 */
  if (worker_tid >= 0) {
    int status = 0;

    print_thread_prefix();
    pr_info("main: joining worker...");

    int rc = thread_join(worker_tid, &status);

    print_thread_prefix();
    pr_info("main: thread_join (worker) returned rc=%x, exit_code=%x", rc, status);
  }

  for (;;) {
    print_thread_prefix();
    pr_info("main loop running...");
    thread_sleep(2);
  }
}
