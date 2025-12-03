// platform/qemu-virt-sbi/uart_16550.c
#include "uart_16550.h"
#include <stdint.h>

#define UART0_BASE  0x10000000UL
#define UART0_RBR   (UART0_BASE + 0)  // Receiver Buffer Register (read)
#define UART0_THR   (UART0_BASE + 0)  // Transmit Holding Register (write)
#define UART0_LSR   (UART0_BASE + 5)  // Line Status Register

#define UART_LSR_DR   0x01  // Data Ready
#define UART_LSR_THRE 0x20  // Transmitter Holding Register Empty

static inline uint8_t uart_lsr_read(void)
{
  volatile uint8_t *lsr = (volatile uint8_t *)UART0_LSR;
  return *lsr;
}

static inline void uart_thr_write(uint8_t v)
{
  volatile uint8_t *thr = (volatile uint8_t *)UART0_THR;
  *thr = v;
}

void uart16550_init(void)
{
  /* QEMU 默认已经初始化好了 16550，这里暂时什么也不做。
   * 如果以后需要设置波特率/数据位，可以在这里补。
   */
}

void uart16550_putc(char c)
{
  /* 等待 TX 空 */
  while ((uart_lsr_read() & UART_LSR_THRE) == 0) {
    /* spin */
  }
  uart_thr_write((uint8_t)c);
}

void uart16550_write(const char *buf, size_t len)
{
  if (!buf || len == 0)
    return;

  for (size_t i = 0; i < len; ++i) {
    uart16550_putc(buf[i]);
  }
}

void uart16550_puts(const char *s)
{
  if (!s)
    return;

  while (*s) {
    if (*s == '\n')
      uart16550_putc('\r');
    uart16550_putc(*s++);
  }
}

void uart16550_put_hex64(uint64_t x)
{
  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (x >> i) & 0xF;
    char c = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    uart16550_putc(c);
  }
}
