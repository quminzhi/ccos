// main.c
#include <stdint.h>
#include "platform.h"
#include "log.h"

/* 每次 timer 中断打印 tick，并安排下一次定时器 */
static void kernel_timer_tick(void)
{
  pr_info("timer tick");
  platform_timer_start_after(10000000UL); // 大约 1s
}

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void kernel_main(long hartid, long dtb_pa)
{
  (void)hartid;
  (void)dtb_pa;

  /* 初始化平台：trap / 中断 / 注册 timer handler */
  platform_init(kernel_timer_tick);

  /* 初始化日志系统（S 模式 bare-metal 版） */
  log_init_baremetal();
  pr_info("hello from S-mode, hart=%ld", hartid);

  /* 启动第一次定时器 */
  platform_timer_start_after(10000000UL);

  for (;;) {
    platform_idle();
  }
}
