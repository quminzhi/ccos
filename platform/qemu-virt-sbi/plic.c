// plic.c
#include "plic.h"

static inline void w32(uintptr_t addr, uint32_t v)
{
  *(volatile uint32_t*)addr = v;
}
static inline uint32_t r32(uintptr_t addr) { return *(volatile uint32_t*)addr; }

void plic_set_priority(uint32_t irq, uint32_t prio)
{
  if (irq == 0) return;
  w32(PLIC_PRIORITY_BASE + 4u * irq, prio);
}

void plic_enable_irq(uint32_t irq)
{
  if (irq >= 32) return;
  uint32_t en = r32(PLIC_SENABLE_HART0);
  en |= (1u << irq);
  w32(PLIC_SENABLE_HART0, en);
}

void plic_disable_irq(uint32_t irq)
{
  if (irq >= 32) return;
  uint32_t en = r32(PLIC_SENABLE_HART0);
  en &= ~(1u << irq);
  w32(PLIC_SENABLE_HART0, en);
}

uint32_t plic_claim(void) { return r32(PLIC_SCLAIM_HART0); }

void plic_complete(uint32_t irq)
{
  if (irq) w32(PLIC_SCLAIM_HART0, irq);
}

void plic_init_s_mode(void)
{
  w32(PLIC_SENABLE_HART0, 0);
  w32(PLIC_STHRESHOLD_HART0, 0);
}
