// main.c
#include <stdint.h>
#include "platform.h"
#include "log.h"
#include "trap.h"
#include "arch.h"
#include "thread.h"

void user_main(void *arg);

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void main(long hartid, long dtb_pa)
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
  platform_timer_start_after(1000000UL);

  /* exec user main */
  threads_exec(user_main, NULL);

  for (;;) {
    __asm__ volatile("wfi");
  }
}
