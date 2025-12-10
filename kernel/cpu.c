#include "cpu.h"
#include <stdint.h>
#include "panic.h"

cpu_t g_cpus[MAX_HARTS];
uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

volatile uint32_t g_boot_hartid = NO_BOOT_HART;
volatile int smp_boot_done      = 0;

void cpu_init_this_hart(uintptr_t hartid) {
  if (hartid >= MAX_HARTS) {
    panic("cpu_init_this_hart: hartid >= MAX_HARTS");
  }

  cpu_t *c  = &g_cpus[hartid];

  c->hartid = hartid;
  c->online = 1;

  /* 用 tp 指向当前 cpu 结构，以后所有 cpu_this() / cpu_current_hartid()
     都通过 tp 间接访问，不再依赖 mhartid CSR。 */
  __asm__ volatile("mv tp, %0" ::"r"(c));
}

void set_smp_boot_done() {
  smp_mb();  // 确保上面所有写操作先对其它 hart 可见
  smp_boot_done = 1;
  smp_mb();  // 防止之后的代码被重排到前面
}

void wait_for_smp_boot_done() {
  while (!smp_boot_done) {
    smp_mb();
    __asm__ volatile("wfi");
  }
  smp_mb();
}
