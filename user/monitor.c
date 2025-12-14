/* monitor.c */

#include <stddef.h>
#include <stdint.h>

#include "monitor.h"
#include "syscall.h"  /* thread_list/thread_create/thread_kill/thread_exit... */
#include "ulib.h"     /* u_printf/sleep/u_strcmp/u_atoi... */
#include "uthread.h"  /* struct u_thread_info, thread_state_name() */

#ifndef THREAD_MAX
#define THREAD_MAX 10
#endif

#define MON_MAX            4

/* Flags: optional filters. */
#define MON_F_USER_ONLY    (1u << 0)  /* Print user threads only. */
#define MON_F_RUNNING_ONLY (1u << 1)  /* Print threads currently running. */
#define MON_F_HIDE_IDLE \
  (1u << 2)  /* Hide tid < MAX_HARTS idle threads per convention. */

typedef struct {
  int      used;
  tid_t    tid;
  uint32_t seq;
  uint32_t period;    /* Period in ticks. */
  int32_t  remaining; /* <0: forever, >=0: countdown. */
  uint32_t flags;     /* Filter/option bitmask. */
} mon_ctx_t;

static mon_ctx_t g_mons[MON_MAX];

/* Format a hart id for table output: -1 -> "---". */
static void
fmt_hart(int hart, char *buf, size_t bufsz)
{
  if (hart >= 0) {
    u_snprintf(buf, bufsz, "%d", hart);
  } else {
    u_snprintf(buf, bufsz, "---");
  }
}

static int
mon_filter_pass(const mon_ctx_t *m, const struct u_thread_info *ti)
{
  if ((m->flags & MON_F_USER_ONLY) && !ti->is_user) {
    return 0;
  }
  if ((m->flags & MON_F_RUNNING_ONLY) && (ti->cpu < 0)) {
    return 0;
  }
  if ((m->flags & MON_F_HIDE_IDLE) && (ti->tid >= 0) &&
      (ti->tid < (int)MAX_HARTS)) {
    return 0;
  }
  return 1;
}

static void
print_threads_table(const mon_ctx_t *m, const struct u_thread_info *infos, int n)
{
  u_printf(" TID  STATE     MODE CPU LAST   MIG      RUNS  NAME\n");
  u_printf(" ---- --------- ---- --- ---- ------ --------- ---------------\n");

  for (int i = 0; i < n; ++i) {
    const struct u_thread_info *ti = &infos[i];
    if (!mon_filter_pass(m, ti)) {
      continue;
    }

    const char *st = thread_state_name(ti->state);
    char mode      = ti->is_user ? 'U' : 'S';

    char cpu_s[4];
    char last_s[5];
    fmt_hart(ti->cpu, cpu_s, sizeof(cpu_s));
    fmt_hart(ti->last_hart, last_s, sizeof(last_s));

    u_printf(" %-4d %-9s  %c   %-3s %-4s %6u %9llu %s\n", ti->tid, st, mode,
             cpu_s, last_s, (unsigned)ti->migrations,
             (unsigned long long)ti->runs, ti->name);
  }
}

static __attribute__((noreturn)) void
monitor_main(void *arg)
{
  mon_ctx_t *m = (mon_ctx_t *)arg;

  /* Note: if the shell kills this thread, the cleanup below might not run. */
  for (;;) {
    if (!m->used) {
      thread_exit(0);
    }

    sleep(m->period);

    struct u_thread_info infos[THREAD_MAX];
    int n = thread_list(infos, THREAD_MAX);
    if (n < 0) {
      u_printf("\n[mon tid=%d] thread_list failed rc=%d\n", (int)m->tid, n);
    } else {
      u_printf("\n[mon tid=%d seq=%u period=%u flags=0x%x]\n", (int)m->tid,
               (unsigned)m->seq++, (unsigned)m->period, (unsigned)m->flags);
      print_threads_table(m, infos, n);
    }

    if (m->remaining >= 0) {
      m->remaining--;
      if (m->remaining == 0) {
        m->used = 0;  /* Normal exit: release the slot. */
        thread_exit(0);
      }
    }
  }
}

static mon_ctx_t *
mon_alloc(void)
{
  for (int i = 0; i < MON_MAX; ++i) {
    if (!g_mons[i].used) {
      g_mons[i].used      = 1;
      g_mons[i].tid       = -1;
      g_mons[i].seq       = 0;
      g_mons[i].period    = 10;
      g_mons[i].remaining = -1;
      g_mons[i].flags     = 0;
      return &g_mons[i];
    }
  }
  return NULL;
}

static mon_ctx_t *mon_find_by_tid(tid_t tid) {
  for (int i = 0; i < MON_MAX; ++i) {
    if (g_mons[i].used && g_mons[i].tid == tid) {
      return &g_mons[i];
    }
  }
  return NULL;
}

/* ====== Shell-visible API ====== */

tid_t mon_start_ex(uint32_t period_ticks, int32_t count, uint32_t flags);

tid_t mon_start(uint32_t period_ticks, int32_t count) {
  return mon_start_ex(period_ticks, count, 0);
}

tid_t mon_start_ex(uint32_t period_ticks, int32_t count, uint32_t flags) {
  if (period_ticks == 0) period_ticks = 1;

  mon_ctx_t *m = mon_alloc();
  if (!m) return -1;

  m->period    = period_ticks;
  m->remaining = count;
  m->flags     = flags;

  tid_t tid    = thread_create(monitor_main, m, "monitor");
  if (tid < 0) {
    m->used = 0;
    return tid;
  }
  m->tid = tid;
  return tid;
}

int mon_stop(tid_t tid) {
  mon_ctx_t *m = mon_find_by_tid(tid);
  if (m) {
    m->used = 0;  /* Let it exit cooperatively if it wakes up. */
  }
  return thread_kill(tid);  /* Fallback: issue a kill directly. */
}

void mon_list(void) {
  u_printf("Active monitors:\n");
  for (int i = 0; i < MON_MAX; ++i) {
    if (g_mons[i].used) {
      u_printf("  tid=%d period=%u remaining=%d seq=%u flags=0x%x\n",
               (int)g_mons[i].tid, (unsigned)g_mons[i].period,
               (int)g_mons[i].remaining, (unsigned)g_mons[i].seq,
               (unsigned)g_mons[i].flags);
    }
  }
}

void mon_once(void) {
  struct u_thread_info infos[THREAD_MAX];
  int n = thread_list(infos, THREAD_MAX);
  if (n < 0) {
    u_printf("mon: thread_list failed rc=%d\n", n);
    return;
  }

  /* A one-shot dump uses no filters (flags = 0). */
  mon_ctx_t fake = {0};
  fake.flags     = 0;
  print_threads_table(&fake, infos, n);
}
