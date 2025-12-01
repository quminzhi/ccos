// log_baremetal.c
#include "log.h"
#include "platform.h"

/* log 系统需要的 writer：把一块 buffer 写到 UART */
static void log_platform_writer(const char *buf, size_t len)
{
  platform_write(buf, len);
}

#if LOG_ENABLE_TIMESTAMP
/* 把 time CSR 转成 ms（QEMU virt: 10MHz，大约 10000 tick ≈ 1ms） */
static uint32_t log_timestamp_ms(void)
{
  uint64_t t = platform_time_now();
  return (uint32_t)(t / 10000ULL);  // 粗略到毫秒即可
}
#endif

void log_init_baremetal(void)
{
  /* 1. 挂上 writer */
  log_init(log_platform_writer);

  /* 2. 设置 runtime 日志等级（也可以直接 LOG_LEVEL_DEBUG） */
  log_set_level(LOG_RUNTIME_DEFAULT_LEVEL);

  /* 3. 路径模式：只打印文件名 basename，比较清爽 */
  log_set_path_mode(LOG_PATH_BASENAME);

#if LOG_ENABLE_TIMESTAMP
  /* 4. 挂上 timestamp 提供函数（可选） */
  log_set_timestamp_fn(log_timestamp_ms);
#endif

  /* 5. 试打一条日志（可选） */
  pr_info("log system initialized (runtime level=%d)", (int)log_get_level());
}
