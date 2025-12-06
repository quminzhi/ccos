#include "thread.h"
#include "syscall.h"
#include "log.h"
#include "ulib.h"

void user_main(void *arg) __attribute__((noreturn));
static void thread_worker(void *arg) __attribute__((noreturn));
static void console_worker(void *arg) __attribute__((noreturn));

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

static void thread_worker(void *arg)
{
  int n = (int)(uintptr_t)arg;

  print_thread_prefix();
  u_puts("worker_thread started, doing some work...");

  /* 模拟“工作一段时间” */
  for (int i = 0; i < n; ++i) {
    print_thread_prefix();
    u_puts("worker_thread step");
    thread_sleep(1); /* sleep 一下，给调度器机会切到别的线程 */
  }

  print_thread_prefix();
  u_puts("worker_thread finished, calling thread_exit");

  /* 把 n 当作 exit_code 传回去，方便主线程在 join 后查看 */
  thread_exit(n);
}

static void console_worker(void *arg)
{
  (void)arg;

  char line[128];

  u_puts("console worker started. type something (\"exit\" to quit):");

  for (;;) {
    int len = u_gets(line, sizeof(line));
    if (len <= 0) {
      // 暂时简单处理：出错/EOF 就继续读
      continue;
    }

    // line 已经没有末尾的 '\n' / '\r' 了
    if (u_strcmp(line, "exit") == 0 || u_strcmp(line, "quit") == 0) {
      u_puts("console worker exiting.");
      thread_exit(0);  // noreturn
    }

    u_printf("you typed: %s\n", line);
  }
}

void user_main(void *arg)
{
  (void)arg;

  print_thread_prefix();
  pr_info("system init done.");

  // assert_test(NULL, 0);

  /* 创建一个 worker 线程，演示 thread_exit / thread_join */
  tid_t worker_tid =
      thread_create(thread_worker, (void *)(uintptr_t)3, "worker");
  if (worker_tid < 0) {
    pr_err("Failed to create worker_thread");
  }

  /* 如果 worker 创建成功，主线程 join 一次，测试 join/exit 正常工作 */
  if (worker_tid >= 0) {
    int status = 0;

    print_thread_prefix();
    pr_info("main: joining worker...");

    int rc = thread_join(worker_tid, &status);

    print_thread_prefix();
    pr_info("main: thread_join (worker) returned rc=%x, exit_code=%x", rc,
            status);
  }

  /* 创建一个 console worker 线程，负责终端 echo */
  tid_t console_tid = thread_create(console_worker, NULL, "console");
  if (console_tid < 0) {
    pr_err("Failed to create console_worker");
  } else {
    int status = 0;

    print_thread_prefix();
    pr_info("main: joining console...");

    int rc = thread_join(console_tid, &status);

    print_thread_prefix();
    pr_info("main: thread_join (console) returned rc=%x, exit_code=%x", rc,
            status);
  }

  for (;;) {
    print_thread_prefix();
    pr_info("main loop running...");
    thread_sleep(2);
  }
}
