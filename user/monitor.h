#ifndef MONITOR_H
#define MONITOR_H

#include "types.h"

tid_t mon_start(uint32_t period_ticks, int32_t count);
int   mon_stop(tid_t tid);
void  mon_list(void);
void  mon_once(void);

#endif /* MONITOR_H */
