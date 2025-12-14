#pragma once

#include <stdint.h>
#include "types.h"

struct trapframe;

/* 时间片长度：每核本地 tick（10ms）累计 5 次后触发一次强制抢占（~50ms）。 */
#define SCHED_SLICE_TICKS 5

/* 每个 hart 初始化（slice 计数等） */
void sched_init_this_hart(uint32_t hartid);

/* S-mode timer interrupt 入口：每个 hart 都会进；必须重设本地 timer。 */
void sched_on_timer_irq(struct trapframe *tf);

/* S-mode software interrupt (IPI) 入口。 */
void sched_on_ipi_irq(struct trapframe *tf);

/* 选择把 runnable 线程投递到哪个 hart。 */
uint32_t sched_pick_target_hart(tid_t tid, uint32_t waker_hart);
