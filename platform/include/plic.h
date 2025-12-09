#ifndef PLIC_H
#define PLIC_H

#include <stdint.h>

/*
 * PLIC MMIO register offsets from the PLIC base address.
 * QEMU virt 上的布局：
 *   base:            reg = <0x0 0x0c000000 0x0 0x4000000>;
 *   priority:        base + 0x000000
 *   pending:         base + 0x001000
 *   hart0 S-mode:    enable      @ base + 0x002080
 *                    threshold   @ base + 0x201000
 *                    claim/comp  @ base + 0x201004
 */

#define PLIC_PRIORITY_OFFSET         0x000000u
#define PLIC_PENDING_OFFSET          0x001000u

/* QEMU virt: hart0 S-mode context offsets */
#define PLIC_SENABLE_HART0_OFFSET    0x002080u
#define PLIC_STHRESHOLD_HART0_OFFSET 0x201000u
#define PLIC_SCLAIM_HART0_OFFSET     0x201004u

void plic_init_s_mode(void);
void plic_set_priority(uint32_t irq, uint32_t prio);
void plic_enable_irq(uint32_t irq);
void plic_disable_irq(uint32_t irq);
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);

#endif  // PLIC_H
