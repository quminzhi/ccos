#include "console.h"
#include "uart_16550.h"
#include <stdint.h>
#include "riscv_csr.h"

#define CONSOLE_RBUF_SIZE 1024

static char g_rx_buf[CONSOLE_RBUF_SIZE];
static volatile uint32_t g_rx_head = 0;  // 下一个写位置
static volatile uint32_t g_rx_tail = 0;  // 下一个读位置

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

/* 中断上下文调用：把字符塞进 ring buffer */
void console_on_char_from_irq(uint8_t ch)
{
  uint32_t next = (g_rx_head + 1) % CONSOLE_RBUF_SIZE;
  if (next == g_rx_tail) {
    // overflow：简单起见，丢字符
    return;
  }
  g_rx_buf[g_rx_head] = (char)ch;
  g_rx_head           = next;
}

/* 非阻塞读：尽量从 ring buffer 拿数据 */
static int console_read_nonblock(char *buf, size_t len)
{
  size_t n = 0;
  while (n < len && !rb_is_empty()) {
    buf[n++]  = g_rx_buf[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1) % CONSOLE_RBUF_SIZE;
  }
  return (int)n;
}

/* 阻塞读：至少读到 1 字节才返回 */
int console_read_blocking(char *buf, size_t len)
{
  if (len == 0) return 0;

  int n = console_read_nonblock(buf, len);
  while (n == 0) {
    // TODO: should go to sleep wait for interrupt to wake up
    /* 没数据：简单版本，用 wfi 省点电，等中断来了再继续 */
    asm volatile("wfi");
    n = console_read_nonblock(buf, len);
  }
  return n;
}
