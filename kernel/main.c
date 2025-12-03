// main.c
#include <stdint.h>
#include "platform.h"
#include "log.h"
#include "trap.h"
#include "arch.h"
#include "thread.h"
#include "syscall.h"

static void print_thread_prefix(void)
{
  tid_t tid        = thread_current();
  const char *name = thread_name(tid);

  platform_putc('[');
  platform_put_hex64((uintptr_t)tid);
  platform_putc(':');
  platform_puts(name);
  platform_puts("] ");
}

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

static void user_main(void *arg)
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
    platform_puts("main: thread_join(worker) returned rc=");
    platform_put_hex64((uintptr_t)rc);
    platform_puts(", exit_code=");
    platform_put_hex64((uintptr_t)status);
    platform_puts("\n");
  }

  /* 启动第一次定时器（在 U 模式启动也可以） */
  platform_timer_start_after(10000000UL);

  for (;;) {
    print_thread_prefix();
    platform_puts("main loop running...\n");
    thread_sleep(10); /* 这里的 ecall 就是 U-mode ECALL，能进 trap.c 了 */
  }
}

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void kernel_main(long hartid, long dtb_pa)
{
  (void)hartid;
  (void)dtb_pa;

  platform_puts("Booting...\n");

  platform_early_init();
  log_init_baremetal();
  trap_init();

  arch_enable_timer_interrupts();
  threads_init();

  /* 启动第一次定时器 */
  platform_timer_start_after(10000000UL);

  /* 启动 user main */
  threads_start(user_main, NULL);

  for (;;) {
    print_thread_prefix();
    platform_puts("main loop running...\n");
    thread_sleep(10);
  }
}
