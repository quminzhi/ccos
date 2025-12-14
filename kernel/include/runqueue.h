#pragma once

#include <stdint.h>
#include "types.h"

/* 简单 per-hart FIFO runqueue（单链表）。*/

void rq_init(uint32_t hartid);
void rq_init_all(void);
void rq_push_tail(uint32_t hartid, tid_t tid);
tid_t rq_pop_head(uint32_t hartid);
uint32_t rq_len(uint32_t hartid);
