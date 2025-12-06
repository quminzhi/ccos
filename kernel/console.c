#include "console.h"
#include "uart_16550.h"
#include <stdint.h>
#include "thread.h"

#define CONSOLE_RBUF_SIZE 1024

static char g_rx_buf[CONSOLE_RBUF_SIZE];
static volatile uint32_t g_rx_head = 0;  // 下一个写位置
static volatile uint32_t g_rx_tail = 0;  // 下一个读位置

/* 当前有哪个线程在等 stdin？没有则为 -1 */
tid_t g_stdin_waiter        = -1;

static inline int rb_is_empty(void) { return g_rx_head == g_rx_tail; }

static inline int rb_is_full(void)
{
  return ((g_rx_head + 1) % CONSOLE_RBUF_SIZE) == g_rx_tail;
}

void console_init(void)
{
  uart16550_init();  // 打开 UART，顺便开 RX 中断
}

/* 给内核 / sys_write 用的输出 */
void console_write(const char *buf, size_t len) { uart16550_write(buf, len); }

/* 非阻塞读：尽量从 ring buffer 拿数据 */
int console_read_nonblock(char *buf, size_t len)
{
  size_t n = 0;
  while (n < len && !rb_is_empty()) {
    buf[n++]  = g_rx_buf[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1) % CONSOLE_RBUF_SIZE;
  }
  return (int)n;
}

int console_read_block_once(char *buf, size_t len, struct trapframe *tf)
{
  if (len == 0) return 0;

  int n = console_read_nonblock(buf, len);
  if (n > 0) {
    return n;  // 直接有数据
  }

  /* 没数据：登记自己是 stdin_waiter，然后 block */
  g_stdin_waiter = thread_current();

  thread_block(tf);  // 当前线程状态→BLOCKED，调度出去
  /* 不会在“当前线程”的语义下继续执行这后面的代码 */

  return 0;  // 这行只是语法需要，逻辑上不会给当前线程用到
}

/* 中断上下文调用：把字符塞进 ring buffer */
void console_on_char_from_irq(uint8_t ch)
{
  /* 1. 先塞到 ring buffer（满了就丢） */
  if (!rb_is_full()) {
    g_rx_buf[g_rx_head] = (char)ch;
    g_rx_head           = (g_rx_head + 1) % CONSOLE_RBUF_SIZE;
  }

  /* 2. 如果当前没有人在等 stdin，就先存着 */
  if (g_stdin_waiter < 0) {
    return;
  }

  /* 3. wake and let g_stdin_waiter read” */
  thread_read_from_stdin(console_read_nonblock);
  g_stdin_waiter = -1;
}
