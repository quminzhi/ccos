#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>

void arch_enable_timer_interrupts(void);
void arch_drop_to_user(void (*entry)(void *), void *arg, uintptr_t user_sp);

#endif // ARCH_H
