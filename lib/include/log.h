#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Convenience init for bare-metal UART platform.
 * Implemented in log_baremetal.cpp.
 * Call this once after Global::uart has been configured.
 */
void log_init_baremetal(void);

/*===========================================================
 *  SIMPLE LOGGING SYSTEM
 *
 *  (说明略，保持你原来的注释)
 *===========================================================*/

/* =========================
 *   CONFIGURATION MACROS
 * ========================= */

/* Log levels (similar to kernel) */
typedef enum {
  LOG_LEVEL_OFF   = 0,
  LOG_LEVEL_ERROR = 1,
  LOG_LEVEL_WARN  = 2,
  LOG_LEVEL_INFO  = 3,
  LOG_LEVEL_DEBUG = 4,
  LOG_LEVEL_TRACE = 5,
} log_level_t;

/* Max level that is compiled in (compile-time filter). */
#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL LOG_LEVEL_DEBUG
#endif

/* Default runtime level (can be changed with log_set_level). */
#ifndef LOG_RUNTIME_DEFAULT_LEVEL
#define LOG_RUNTIME_DEFAULT_LEVEL LOG_LEVEL_INFO
#endif

/* Controls how the 'file' component (from __FILE__) is printed. */
typedef enum {
  LOG_PATH_NONE     = 0, /* do not print file at all */
  LOG_PATH_BASENAME = 1, /* print only basename, e.g. "minit.cpp" */
  LOG_PATH_FULL     = 2, /* print full __FILE__ string */
} log_path_mode_t;

#ifndef LOG_DEFAULT_PATH_MODE
#define LOG_DEFAULT_PATH_MODE LOG_PATH_BASENAME
#endif /* LOG_DEFAULT_PATH_MODE */

/* Enable/disable timestamp support. */
#ifndef LOG_ENABLE_TIMESTAMP
#define LOG_ENABLE_TIMESTAMP 1
#endif

/* Enable/disable in-RAM ring buffer. */
#ifndef LOG_USE_RING_BUFFER
#define LOG_USE_RING_BUFFER 1
#endif

/* Size of temporary formatting buffer for one log line. */
#ifndef LOG_BUFFER_SIZE
#define LOG_BUFFER_SIZE 256
#endif

/* Ring buffer size in bytes (for storing log output). */
#ifndef LOG_RING_BUFFER_SIZE
#define LOG_RING_BUFFER_SIZE 2048
#endif

/* =========================
 *        TYPES & API
 * ========================= */

/* Low-level writer: you implement this (e.g. UART, SWO, semihosting). */
typedef void (*log_write_fn_t)(const char* buf, size_t len);

/* Initialize logging with a writer (must be called before using pr_*). */
void log_init(log_write_fn_t fn);

/* Runtime level control. */
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

/* Control whether and how file paths are printed. */
void log_set_path_mode(log_path_mode_t mode);
log_path_mode_t log_get_path_mode(void);

/* Convert level to short string ("E", "W", "I", "D", "T"). */
const char* log_level_to_string(log_level_t level);

/* Core logging functions (normally use pr_* macros instead). */
int log_vprintf(log_level_t level, const char* file, int line, const char* func,
                const char* fmt, va_list ap);

int log_printf(log_level_t level, const char* file, int line, const char* func,
               const char* fmt, ...);

/* ===== Timestamps ===== */
#if LOG_ENABLE_TIMESTAMP
/* Timestamp callback: return time in ticks or ms, up to you. */
typedef uint32_t (*log_timestamp_fn_t)(void);

/* Set timestamp provider. If NULL, no timestamp is printed. */
void log_set_timestamp_fn(log_timestamp_fn_t fn);
#endif /* LOG_ENABLE_TIMESTAMP */

/* ===== Ring buffer access (kernel-like log buffer) ===== */
#if LOG_USE_RING_BUFFER
/* Read and consume up to maxlen bytes from ring buffer. */
size_t log_ring_read(char* out, size_t maxlen);

/* Peek (non-destructive) up to maxlen bytes from ring buffer. */
size_t log_ring_peek(char* out, size_t maxlen);

/* Current used size and total capacity of ring buffer. */
size_t log_ring_size(void);
size_t log_ring_capacity(void);

/* Clear the ring buffer. */
void log_ring_clear(void);
#endif /* LOG_USE_RING_BUFFER */

/* ===== Hexdump helper ===== */

/* Hexdump with log prefix (level/file/line/func etc). */
void log_hexdump(log_level_t level, const char* file, int line,
                 const char* func, const void* data, size_t len,
                 const char* prefix);

/* =========================
 *   LOCKING HOOKS
 * ========================= */

/* Optional lock/unlock hooks for multi-core/IRQ safety.
 * You can override these macros before including log.h, e.g.:
 *
 *   #define LOG_LOCK()   uint32_t flags = irq_disable()
 *   #define LOG_UNLOCK() irq_restore(flags)
 */
#ifndef LOG_LOCK
#define LOG_LOCK() \
  do {             \
  } while (0)
#endif

#ifndef LOG_UNLOCK
#define LOG_UNLOCK() \
  do {               \
  } while (0)
#endif

/* =========================
 *       PRINT MACROS
 * ========================= */

/* Internal helper: do not use directly. */
#define LOG_DO_PRINT(level, fmt, ...)                                        \
  do {                                                                       \
    if ((level) <= LOG_COMPILE_LEVEL) {                                      \
      log_printf((level), __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    }                                                                        \
  } while (0)

/* printk-style macros */
#if LOG_COMPILE_LEVEL >= LOG_LEVEL_ERROR
#define pr_err(fmt, ...) LOG_DO_PRINT(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#else
#define pr_err(fmt, ...) \
  do {                   \
  } while (0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_LEVEL_WARN
#define pr_warn(fmt, ...) LOG_DO_PRINT(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#else
#define pr_warn(fmt, ...) \
  do {                    \
  } while (0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_LEVEL_INFO
#define pr_info(fmt, ...) LOG_DO_PRINT(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#else
#define pr_info(fmt, ...) \
  do {                    \
  } while (0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_LEVEL_DEBUG
#define pr_debug(fmt, ...) LOG_DO_PRINT(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
  do {                     \
  } while (0)
#endif

#if LOG_COMPILE_LEVEL >= LOG_LEVEL_TRACE
#define pr_trace(fmt, ...) LOG_DO_PRINT(LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#else
#define pr_trace(fmt, ...) \
  do {                     \
  } while (0)
#endif

/* Hexdump macro.
 * 'level' is one of LOG_LEVEL_*, 'prefix' can be NULL or a short string.
 */
#define pr_hexdump(level, prefix, data, length)                            \
  do {                                                                     \
    if ((level) <= LOG_COMPILE_LEVEL) {                                    \
      log_hexdump((level), __FILE__, __LINE__, __func__, (data), (length), \
                  (prefix));                                               \
    }                                                                      \
  } while (0)

/* =========================
 *       PANIC / ASSERT
 * ========================= */

/* Panic with formatted message, file/line 自动带上。
 * 内部会打印一条日志然后死循环，并在 RISC-V 上触发 ebreak。
 */
void log_panicf_internal(const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4))) __attribute__((noreturn));
#else
    ;
#endif

#define PANICF(fmt, ...) \
  log_panicf_internal(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define PANIC() log_panicf_internal(__FILE__, __LINE__, "PANIC")

/* 断言：Debug 版本启用，Release（定义 NDEBUG）下为空操作。 */
#ifndef NDEBUG

#define ASSERT(cond)                      \
  do {                                    \
    if (!(cond)) {                        \
      PANICF("ASSERT failed: %s", #cond); \
    }                                     \
  } while (0)

#define ASSERTF(cond, fmt, ...)                                 \
  do {                                                          \
    if (!(cond)) {                                              \
      PANICF("ASSERT failed: %s | " fmt, #cond, ##__VA_ARGS__); \
    }                                                           \
  } while (0)

#else /* NDEBUG */

#define ASSERT(cond)    \
  do {                  \
    (void)sizeof(cond); \
  } while (0)

#define ASSERTF(cond, fmt, ...) \
  do {                          \
    (void)sizeof(cond);         \
  } while (0)

#endif /* NDEBUG */

/* =========================
 *      BREAK IF / EBREAK
 * ========================= */

#ifndef NDEBUG

static inline void log_break(void)
{
#ifdef __riscv
  __asm__ volatile("ebreak");
#endif
}

/* 条件满足时直接 ebreak，停在调试器里 */
#define BREAK_IF(cond) \
  do {                 \
    if (cond) {        \
      log_break();     \
    }                  \
  } while (0)

#else /* Release 版：不 ebreak，只打一条 warning */

static inline void log_break(void)
{
  /* Release 版本不做任何事情，防止量产环境误触发断点异常 */
}

/* 条件满足时打一条 warning 日志继续跑 */
#define BREAK_IF(cond)                                    \
  do {                                                    \
    if (cond) {                                           \
      pr_warn("BREAK_IF hit (release build): %s", #cond); \
    }                                                     \
  } while (0)

#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
