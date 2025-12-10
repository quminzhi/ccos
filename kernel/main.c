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
#include "sbi.h"

extern void secondary_entry(void);

static void primary_main(long hartid, long dtb_pa) __attribute__((noreturn));
static void secondary_main(long hartid, long dtb_pa) __attribute__((noreturn));

void user_main(void *arg) __attribute__((noreturn));

/*
 * S 模式入口：OpenSBI 会以 S-mode 跳到 _start，
 * start.S 里会清 BSS + 建栈，然后 tail 调用 main(hartid, dtb)
 */
void kernel_main(long hartid, long dtb_pa) {
  uint32_t my_hartid = (uint32_t)hartid;
  uint32_t expected  = NO_BOOT_HART;

  cpu_init_this_hart(hartid);

  /* 第一个set g_boot_hartid的就是logic boot hart */
  if (__sync_bool_compare_and_swap(&g_boot_hartid, expected, my_hartid)) {
    primary_main(hartid, dtb_pa);
  } else {
    wait_for_smp_boot_done();
    secondary_main(hartid, dtb_pa);
  }
}

/* opensbi: 其他hart在M模式等待，需要显示开启 */
static void start_other_harts(long dtb_pa) {
  ASSERT(g_boot_hartid != NO_BOOT_HART);
  for (uint32_t h = 0; h < MAX_HARTS; ++h) {
    if (h == (uint32_t)g_boot_hartid) continue;
    struct sbiret ret = sbi_hart_start(h, (uintptr_t)secondary_entry,
                                       (uintptr_t)dtb_pa /* opaque -> a1 */);
    if (ret.error != 0) {
      pr_warn("sbi_hart_start(hart=%u) failed: err=%ld\n", h, ret.error);
    }
  }
}

void primary_main(long hartid, long dtb_pa) {
  platform_init((uintptr_t)hartid, (uintptr_t)dtb_pa);
  platform_boot_hart_init((uintptr_t)hartid);

  /* platform_puts can be used after platform_init (uart) */
  platform_puts("Booting...\n");

  trap_init();
  console_init();  // console layer on uart
  log_init_baremetal();

  time_init();
  threads_init();

  set_smp_boot_done();
  start_other_harts(dtb_pa);

  arch_enable_timer_interrupts();
  arch_enable_external_interrupts();

  platform_timer_start_after(DELTA_TICKS);
  // platform_rtc_set_alarm_after(3ULL * 1000 * 1000 * 1000);

  pr_info("Kernel built as %s, CPUS=%d, Boot Hart=%ld", KERNEL_BUILD_TYPE,
          MAX_HARTS, (long)hartid);
  pr_info("system init done, starting user main...");
  thread_exec(user_main, NULL);

  for (;;) {
    __asm__ volatile("wfi");
  }
}

void secondary_main(long hartid, long dtb_pa) {
  (void)hartid;
  (void)dtb_pa;

  platform_secondary_hart_init(hartid);
  trap_init();

  // 开timer就参与调度了，除非注册自己的timer handler

  pr_info("hart %ld online (secondary)", cpu_current_hartid());

  for (;;) {
    __asm__ volatile("wfi");
  }
}
