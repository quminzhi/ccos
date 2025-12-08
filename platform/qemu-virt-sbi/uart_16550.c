// platform/qemu-virt-sbi/uart_16550.c
#include "uart_16550.h"
#include "platform.h"
#include <stdint.h>
#include "fdt_helper.h"

static volatile uint8_t *uart_base;
static uint32_t uart_irq;

static inline void uart_w(uint32_t off, uint8_t v) { uart_base[off] = v; }
static inline uint8_t uart_r(uint32_t off) { return uart_base[off]; }

// #define UART0_BASE     0x10000000UL
#define UART_RBR       0u  // Receiver Buffer Register
#define UART_THR       0u  // Transmit Holding Register
#define UART_IER       1u  // Interrupt Enable Register
#define UART_LSR       5u  // Line Status Register

#define UART_LSR_DR    0x01  // Data Ready
#define UART_LSR_THRE  0x20  // Transmitter Holding Register Empty
#define UART_IER_ERBFI 0x01  // Enable Received Data Available Interrupt

extern void console_on_char_from_irq(uint8_t ch);

static inline uint8_t uart_lsr_read(void) { return uart_r(UART_LSR); }
static inline void uart_thr_write(uint8_t v) { uart_w(UART_THR, v); }
static inline uint8_t uart_rbr_read(void) { return uart_r(UART_RBR); }
static inline void uart_ier_write(uint8_t v) { uart_w(UART_IER, v); }

uint32_t uart16550_get_irq(void) { return uart_irq; }

void uart16550_init(void)
{
  const void *fdt = platform_get_dtb();
  uint64_t base, size;
  uint32_t irq;

  if (fdt_find_reg_by_compat(fdt, "ns16550a", &base, &size) < 0) {
    platform_puts("uart16550_init: no ns16550a reg in fdt\n");
    return;
  }

  if (fdt_find_irq_by_compat(fdt, "ns16550a", &irq) < 0) {
    platform_puts("uart16550_init: no ns16550a interrupts in fdt\n");
    return;
  }

  uart_base = (volatile uint8_t *)(uintptr_t)base;
  uart_irq  = irq;

  // baud rate setting or others
  uart_ier_write(UART_IER_ERBFI);

  platform_register_irq_handler(uart_irq, uart16550_irq_handler);
}

void uart16550_irq_handler(void)
{
  while (uart_lsr_read() & UART_LSR_DR) {
    uint8_t ch = uart_rbr_read();
    console_on_char_from_irq(ch);
  }
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
  if (!buf || len == 0) return;

  for (size_t i = 0; i < len; ++i) {
    uart16550_putc(buf[i]);
  }
}

void uart16550_puts(const char *s)
{
  if (!s) return;

  while (*s) {
    if (*s == '\n') uart16550_putc('\r');
    uart16550_putc(*s++);
  }
}

void uart16550_put_hex64(uint64_t x)
{
  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (x >> i) & 0xF;
    char c      = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    uart16550_putc(c);
  }
}
