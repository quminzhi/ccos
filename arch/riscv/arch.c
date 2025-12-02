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
