#pragma once

#include <stddef.h>
#include <stdint.h>

#include "types.h"

/* 简单 per-hart FIFO runqueue（单链表）。*/

void rq_init(uint32_t hartid);
void rq_init_all(void);
void rq_push_tail(uint32_t hartid, tid_t tid);
tid_t rq_pop_head(uint32_t hartid);
uint32_t rq_len(uint32_t hartid);

/* Remove a specific tid from a hart's runqueue.
 * Returns 0 on success, -1 if not found/invalid.
 */
int rq_remove(uint32_t hartid, tid_t tid);

/* Remove tid from whichever hart runqueue contains it.
 * Returns hartid (>=0) on success, -1 if not found.
 */
int rq_remove_any(tid_t tid);

/* Copy runqueue order into dst (up to max entries); returns length or -1. */
int rq_snapshot(uint32_t hartid, tid_t *dst, size_t max);
