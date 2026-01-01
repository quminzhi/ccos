#ifndef TIMER_H
#define TIMER_H

#include "platform.h"

void timer_init(uintptr_t hartid);
platform_time_t timer_now(void);
void timer_start_at(platform_time_t when);
void timer_start_after(platform_time_t delta);
void timer_stop(void);

#endif  /* TIMER_H */
