/* main.c */
#include <stdint.h>

#include "arch.h"
#include "console.h"
#include "cpu.h"
#include "kernel.h"
#include "log.h"
#include "platform.h"
#include "sbi.h"
#include "thread.h"
#include "time.h"
#include "trap.h"

extern void secondary_entry(void);

static void primary_main(long hartid, long dtb_pa) __attribute__((noreturn));
static void secondary_main(long hartid, long dtb_pa) __attribute__((noreturn));

void user_main(void *arg) __attribute__((noreturn));

/* -------------------------------------------------------------------------- */
/* Early SBI console helpers (no dependency on platform_init/console_init).  */
static void sbi_put_hex64(uint64_t v) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i) {
    sbi_debug_console_write_byte((uint8_t)hex[(v >> (i * 4)) & 0xf]);
  }
}

static void sbi_put_dec(uint64_t v) {
  char buf[32];
  int idx = 0;
  if (v == 0) {
    sbi_debug_console_write_byte('0');
    return;
  }
  while (v && idx < (int)sizeof(buf)) {
    buf[idx++] = (char)('0' + (v % 10));
    v /= 10;
  }
  while (--idx >= 0) {
    sbi_debug_console_write_byte((uint8_t)buf[idx]);
  }
}

static void sbi_early_banner(long hartid, long dtb_pa) {
  sbi_console_puts("kernel_main entry hart=");
  sbi_put_dec((uint64_t)hartid);
  sbi_console_puts(" dtb_pa=0x");
  sbi_put_hex64((uint64_t)dtb_pa);
  sbi_console_puts("\n");
}

/*
 * S-mode entry: OpenSBI jumps to _start, start.S clears BSS + builds the stack,
 * then tail-calls main(hartid, dtb).
 */
void kernel_main(long hartid, long dtb_pa) {
  sbi_early_banner(hartid, dtb_pa);

  uint32_t my_hartid = (uint32_t)hartid;
  uint32_t expected  = NO_BOOT_HART;

  cpu_init_this_hart(hartid);

  /* First hart to set g_boot_hartid becomes the logical boot hart */
  if (__sync_bool_compare_and_swap(&g_boot_hartid, expected, my_hartid)) {
    primary_main(hartid, dtb_pa);
  } else {
    wait_for_smp_boot_done();
    secondary_main(hartid, dtb_pa);
  }
}

/* OpenSBI keeps other harts parked in M-mode; need to start them explicitly */
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
  /*
   * Boot flow:
   *   1) Boot hart: init platform + IRQ + trap + logging + time + threads.
   *   2) Boot hart: mark smp_boot_done, then start other harts via SBI HSM.
   *   3) Secondary hart: run platform_secondary_hart_init() + trap_init(), enable
   *      SSIP/STIP/SEIP, enter idle and wait for IPI/timer/PLIC.
   *   4) Scheduling: only the boot hart drives periodic ticks; whoever makes a
   *      thread RUNNABLE wakes the target hart via IPI (SSIP).
   */
  platform_init((uintptr_t)hartid, (uintptr_t)dtb_pa);
  platform_boot_hart_init((uintptr_t)hartid);

  /* platform_puts can be used after platform_init (uart) */
  platform_puts("Booting...\n");

  trap_init();
  console_init();  /* console layer on uart */

  log_init_baremetal();

  time_init();

  threads_init(user_main);

  set_smp_boot_done();
  start_other_harts(dtb_pa);

  pr_info("Kernel built as %s, CPUS=%d, Boot Hart=%ld", KERNEL_BUILD_TYPE,
          MAX_HARTS, (long)hartid);
  pr_info("Boot Hart: system init done.");

  cpu_enter_idle(hartid);
}

void secondary_main(long hartid, long dtb_pa) {
  (void)hartid;
  (void)dtb_pa;

  platform_secondary_hart_init(hartid);
  trap_init();

  pr_info("hart %ld online (secondary)", cpu_current_hartid());
  cpu_enter_idle((uint32_t)hartid);
}
