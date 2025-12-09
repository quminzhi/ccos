// main.c
#include <stdint.h>
#include "console.h"
#include "platform.h"
#include "log.h"
#include "trap.h"
#include "arch.h"
#include "thread.h"
#include "time.h"

void user_main(void *arg) __attribute__((noreturn));

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void kernel_main(long hartid, long dtb_pa)
{
  (void)hartid;

  platform_init(dtb_pa);
  platform_puts("Booting...\n");

  trap_init();
  console_init();  // console layer on uart
  log_init_baremetal();

  time_init();
  threads_init();

  arch_enable_timer_interrupts();
  arch_enable_external_interrupts();

  platform_timer_start_after(DELTA_TICKS);
  platform_rtc_set_alarm_after(3ULL * 1000 * 1000 * 1000);

  pr_info("system init done, starting user main...");
  threads_exec(user_main, NULL);

  for (;;) {
    __asm__ volatile("wfi");
  }
}
