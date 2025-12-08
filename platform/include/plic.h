// plic.h
#ifndef PLIC_H
#define PLIC_H

#include <stdint.h>

#define PLIC_BASE             0x0C000000UL
#define PLIC_PRIORITY_BASE    (PLIC_BASE + 0x000000)
#define PLIC_PENDING_BASE     (PLIC_BASE + 0x001000)

/* QEMU virt: hart0 S-mode context */
#define PLIC_SENABLE_HART0    (PLIC_BASE + 0x002080)
#define PLIC_STHRESHOLD_HART0 (PLIC_BASE + 0x201000)
#define PLIC_SCLAIM_HART0     (PLIC_BASE + 0x201004)

// 通过fdt读取dtb获取
// #define PLIC_IRQ_UART0        10U
// #define PLIC_IRQ_RTC          11U

void plic_init_s_mode(void);
void plic_set_priority(uint32_t irq, uint32_t prio);
void plic_enable_irq(uint32_t irq);
void plic_disable_irq(uint32_t irq);
uint32_t plic_claim(void);
void plic_complete(uint32_t irq);

#endif  // PLIC_H
