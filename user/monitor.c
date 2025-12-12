#include <stdint.h>
#include "monitor.h"
#include "ulib.h"          // 你自己的用户态头：u_printf/sleep/thread_xxx/thread_list...
#include "uthread.h"
#include "syscall.h"

#ifndef THREAD_MAX
#define THREAD_MAX  10
#endif

#define MON_MAX 4

typedef struct {
  int      used;
  tid_t    tid;
  uint32_t seq;
  uint32_t period;     // ticks
  int32_t  remaining;  // <0: forever, >=0: countdown
  uint32_t flags;      // 预留：比如只打印用户线程、只打印非 idle...
} mon_ctx_t;

static mon_ctx_t g_mons[MON_MAX];

static void print_threads_table(const struct u_thread_info *infos, int n) {
  u_printf(" TID  STATE     MODE  EXIT   NAME\n");
  u_printf(" ---- --------- ----  ----- ------------\n");
  for (int i = 0; i < n; ++i) {
    const struct u_thread_info *ti = &infos[i];
    const char *st = thread_state_name(ti->state);
    char mode = ti->is_user ? 'U' : 'S';
    u_printf(" %-4d %-9s  %c   %5d %s\n",
             ti->tid, st, mode, ti->exit_code, ti->name);
  }
}

static __attribute__((noreturn)) void monitor_main(void *arg) {
  mon_ctx_t *m = (mon_ctx_t *)arg;

  // 注意：如果 shell 里 kill 掉该线程，下面不一定会走到 clean up
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
      u_printf("\n[mon tid=%d seq=%u period=%u]\n",
               (int)m->tid, (unsigned)m->seq++, (unsigned)m->period);
      print_threads_table(infos, n);
    }

    if (m->remaining >= 0) {
      m->remaining--;
      if (m->remaining == 0) {
        // 正常退出：释放 slot
        m->used = 0;
        thread_exit(0);
      }
    }
  }
}

static mon_ctx_t *mon_alloc(void) {
  for (int i = 0; i < MON_MAX; ++i) {
    if (!g_mons[i].used) {
      g_mons[i].used = 1;
      g_mons[i].tid = -1;
      g_mons[i].seq = 0;
      g_mons[i].period = 10;
      g_mons[i].remaining = -1;
      g_mons[i].flags = 0;
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

/* ====== 给 shell 调用的 API ====== */

tid_t mon_start(uint32_t period_ticks, int32_t count) {
  if (period_ticks == 0) period_ticks = 1;

  mon_ctx_t *m = mon_alloc();
  if (!m) return -1;

  m->period = period_ticks;
  m->remaining = count;

  tid_t tid = thread_create(monitor_main, m, "monitor");
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
    m->used = 0;          // 让它“自杀式”退出（如果醒得过来）
  }
  return thread_kill(tid); // 保险：直接 kill
}

void mon_list(void) {
  u_printf("Active monitors:\n");
  for (int i = 0; i < MON_MAX; ++i) {
    if (g_mons[i].used) {
      u_printf("  tid=%d period=%u remaining=%d seq=%u\n",
               (int)g_mons[i].tid,
               (unsigned)g_mons[i].period,
               (int)g_mons[i].remaining,
               (unsigned)g_mons[i].seq);
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
  print_threads_table(infos, n);
}
