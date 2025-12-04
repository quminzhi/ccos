#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

void arch_enable_timer_interrupts(void);

struct trapframe;
void arch_first_switch(struct trapframe *tf);

#endif // ARCH_H
