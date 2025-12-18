// log_baremetal.c
#include "log.h"
#include "platform.h"

/* Writer for log system: send a buffer to UART with CRLF translation */
static void log_platform_writer(const char *buf, size_t len)
{
  if (!buf || len == 0) return;
  for (size_t i = 0; i < len; ++i) {
    char c = buf[i];
    if (c == '\n') platform_putc('\r');
    platform_putc(c);
  }
}

#if LOG_ENABLE_TIMESTAMP
/* Convert time CSR ticks to ms (QEMU virt: 10MHz ≈ 10000 ticks per ms) */
static uint32_t log_timestamp_ms(void)
{
  uint64_t t = platform_time_now();
  return (uint32_t)(t / 10000ULL);  // Rough ms resolution is enough
}
#endif

void log_init_baremetal(void)
{
  /* 1. Install writer */
  log_init(log_platform_writer);

  /* 2. Set runtime log level */
  log_set_level(LOG_RUNTIME_DEFAULT_LEVEL);

  /* 3. Path mode: print basename only */
  log_set_path_mode(LOG_DEFAULT_PATH_MODE);

#if LOG_ENABLE_TIMESTAMP
  /* 4. Install timestamp provider (optional) */
  log_set_timestamp_fn(log_timestamp_ms);
#endif

  /* 5. Test log with level string */
  log_level_t lvl = log_get_level();
  pr_info("log system initialized (runtime level=%d/%s)",
          (int)lvl, log_level_to_full_string(lvl));
}
