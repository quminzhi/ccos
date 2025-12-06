// main.c
#include <stdint.h>
#include "console.h"
#include "platform.h"
#include "log.h"
#include "trap.h"
#include "arch.h"
#include "thread.h"

void user_main(void *arg) __attribute__((noreturn));

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void main(long hartid, long dtb_pa)
{
  (void)hartid;
  (void)dtb_pa;

  platform_puts("Booting...\n");

  platform_uart_init();
  console_init();  // console layer on uart
  log_init_baremetal();

  trap_init();
  platform_plic_init();

  arch_enable_timer_interrupts();
  threads_init();

  /* 启动第一次定时器 */
  platform_timer_start_after(DELTA_TICKS);

  /* exec user main */
  threads_exec(user_main, NULL);

  for (;;) {
    __asm__ volatile("wfi");
  }
}
