#include "cpu.h"
#include <stdint.h>
#include "panic.h"

cpu_t g_cpus[MAX_HARTS];
uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

#include "cpu.h"

volatile int smp_boot_done = 0;

void cpu_init_this_hart(uintptr_t hartid)
{
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
