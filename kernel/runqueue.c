/* runqueue.c */

#include "log.h"
#include "runqueue.h"
#include "thread.h"
#include "types.h"

typedef struct {
  tid_t head;
  tid_t tail;
  uint32_t len;
} runqueue_t;

static runqueue_t g_runqueues[MAX_HARTS];

void rq_init(uint32_t hartid) {
  if (hartid >= (uint32_t)MAX_HARTS) return;
  g_runqueues[hartid].head = -1;
  g_runqueues[hartid].tail = -1;
  g_runqueues[hartid].len  = 0;
}

void rq_init_all(void) {
  for (uint32_t h = 0; h < (uint32_t)MAX_HARTS; ++h) {
    rq_init(h);
  }
}

static inline runqueue_t *
rq(uint32_t hartid)
{
  return &g_runqueues[hartid];
}

void
rq_push_tail(uint32_t hartid, tid_t tid)
{
  if (hartid >= (uint32_t)MAX_HARTS) return;
  if (tid < 0 || tid >= THREAD_MAX) return;
  if (tid < (tid_t)MAX_HARTS) return;  /* 不把 idle 放进 rq */

  runqueue_t* r = rq(hartid);
  Thread* t     = &g_threads[tid];

  if (t->on_rq) {
    PANICF("rq_push_tail: tid=%d already on rq", (int)tid);
  }

  t->rq_next = -1;
  if (r->tail == -1) {
    r->head = r->tail = tid;
  } else {
    g_threads[r->tail].rq_next = tid;
    r->tail                    = tid;
  }
  t->on_rq = 1;
  r->len++;
}

tid_t
rq_pop_head(uint32_t hartid)
{
  if (hartid >= (uint32_t)MAX_HARTS) return -1;
  runqueue_t* r = rq(hartid);
  tid_t tid     = r->head;
  if (tid < 0) return -1;

  Thread* t = &g_threads[tid];
  tid_t nxt = t->rq_next;

  r->head = nxt;
  if (nxt < 0) {
    r->tail = -1;
  }
  t->rq_next = -1;
  t->on_rq   = 0;
  if (r->len > 0) r->len--;
  return tid;
}

uint32_t
rq_len(uint32_t hartid)
{
  if (hartid >= (uint32_t)MAX_HARTS) return 0;
  return rq(hartid)->len;
}

int
rq_remove(uint32_t hartid, tid_t tid)
{
  if (hartid >= (uint32_t)MAX_HARTS) return -1;
  if (tid < 0 || tid >= THREAD_MAX) return -1;

  runqueue_t* r = rq(hartid);
  tid_t cur     = r->head;
  tid_t prev    = -1;

  while (cur >= 0) {
    if (cur == tid) {
      Thread* t = &g_threads[cur];
      tid_t nxt = t->rq_next;

      if (prev < 0) {
        r->head = nxt;
      } else {
        g_threads[prev].rq_next = nxt;
      }
      if (r->tail == cur) {
        r->tail = prev;
      }

      t->rq_next = -1;
      t->on_rq   = 0;
      if (r->len > 0) r->len--;
      return 0;
    }
    prev = cur;
    cur  = g_threads[cur].rq_next;
  }
  return -1;
}

int
rq_remove_any(tid_t tid)
{
  if (tid < 0 || tid >= THREAD_MAX) return -1;
  for (uint32_t h = 0; h < (uint32_t)MAX_HARTS; ++h) {
    if (rq_remove(h, tid) == 0) {
      return (int)h;
    }
  }
  return -1;
}

int
rq_snapshot(uint32_t hartid, tid_t *dst, size_t max)
{
  if (hartid >= (uint32_t)MAX_HARTS) return -1;
  if (!dst || max == 0) return -1;

  runqueue_t* r = rq(hartid);
  tid_t cur     = r->head;
  size_t n      = 0;

  while (cur >= 0 && n < max) {
    dst[n++] = cur;
    cur      = g_threads[cur].rq_next;
  }
  if (n < max) {
    dst[n] = -1;  /* sentinel for user-friendly printing */
  }
  return (int)n;
}
