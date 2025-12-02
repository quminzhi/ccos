// main.c
#include <stdint.h>
#include "platform.h"
#include "log.h"
#include "trap.h"
#include "arch.h"

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void kernel_main(long hartid, long dtb_pa)
{
  (void)hartid;
  (void)dtb_pa;

  platform_early_init();
  log_init_baremetal();
  trap_init();
  arch_enable_timer_interrupts();

  pr_info("hello from S-mode, hart=%ld", hartid);

  /* 启动第一次定时器 */
  platform_timer_start_after(10000000UL);

  for (;;) {
    platform_idle();
  }
}
