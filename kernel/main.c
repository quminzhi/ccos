// main.c
#include <stdint.h>
#include "console.h"
#include "platform.h"
#include "log.h"
#include "trap.h"
#include "arch.h"
#include "thread.h"
#include "time.h"
#include "kernel.h"
#include "cpu.h"

void user_main(void *arg) __attribute__((noreturn));

static void primary_main(long hartid, long dtb_pa);
static void secondary_main(long hartid, long dtb_pa);

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void kernel_main(long hartid, long dtb_pa)
{
  cpu_init_this_hart(hartid);

  if (hartid == 0) {
    primary_main(hartid, dtb_pa);
  } else {
    while (!smp_boot_done) {
      smp_mb();
      __asm__ volatile("wfi");
    }
    smp_mb();
    secondary_main(hartid, dtb_pa);
  }

  for (;;) {
    __asm__ volatile("wfi");
  }
}

void primary_main(long hartid, long dtb_pa)
{
  platform_init((uintptr_t)hartid, (uintptr_t)dtb_pa);
  platform_puts("Booting...\n");

  trap_init();
  console_init();  // console layer on uart
  log_init_baremetal();

  time_init();
  threads_init();

  smp_mb();  // 确保上面所有写操作先对其它 hart 可见
  smp_boot_done = 1;
  smp_mb();  // 防止之后的代码被重排到前面

  arch_enable_timer_interrupts();
  arch_enable_external_interrupts();

  platform_timer_start_after(DELTA_TICKS);
  platform_rtc_set_alarm_after(3ULL * 1000 * 1000 * 1000);

  pr_info("Kernel built as %s, CPUS=%d", KERNEL_BUILD_TYPE, MAX_HARTS);
  pr_info("hart %ld system init done, starting user main...", (long)hartid);
  threads_exec(user_main, NULL);
}

void secondary_main(long hartid, long dtb_pa)
{
  (void)hartid;
  (void)dtb_pa;
  trap_init();

  pr_info("hart %ld online (secondary)", (long)hartid);
}
