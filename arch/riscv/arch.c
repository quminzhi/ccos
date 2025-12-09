#include "arch.h"
#include "riscv_csr.h"

void arch_enable_timer_interrupts(void)
{
  reg_t sstatus = csr_read(sstatus);
  sstatus |= SSTATUS_SIE;
  csr_write(sstatus, sstatus);

  reg_t sie = csr_read(sie);
  sie |= SIE_STIE;
  csr_write(sie, sie);
}

void arch_enable_external_interrupts(void)
{
  csr_set(sie, SIE_SEIE);
  csr_set(sstatus, SSTATUS_SIE);
}
