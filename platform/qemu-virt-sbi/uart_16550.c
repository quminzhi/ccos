/* platform/qemu-virt-sbi/uart_16550.c */
#include "uart_16550.h"
#include "platform.h"
#include <stdint.h>
#include "fdt_helper.h"
#include "libfdt.h"

#include "log.h"
#include "cpu.h"

#define UART_RBR       0u  /* Receive Buffer Register (read) */
#define UART_THR       0u  /* Transmit Holding Register (write) */
#define UART_IER       1u  /* Interrupt Enable Register */
#define UART_IIR       2u  /* Interrupt Identification Register (read) */
#define UART_FCR       2u  /* FIFO Control Register (write) */
#define UART_LSR       5u  /* Line Status Register */
#define UART_MSR       6u  /* Modem Status Register */

/* LSR bits */
#define UART_LSR_DR    0x01u  /* Data Ready */
#define UART_LSR_THRE  0x20u  /* THR Empty */

/* IER bits */
#define UART_IER_ERBFI 0x01u  /* Enable RX interrupt (RBR full / timeout) */
#define UART_IER_ETBEI \
  0x02u  /* Enable THRE interrupt (TX empty)  <-- 如果不用 TX IRQ，建议关掉 */

/* IIR bits / codes */
#define UART_IIR_NO_PENDING 0x01u  /* bit0=1 => no interrupt pending */
#define UART_IIR_ID_MASK    0x0Fu  /* low nibble is interrupt ID */

#define UART_IIR_ID_MSR     0x00u  /* Modem status */
#define UART_IIR_ID_THRE    0x02u  /* THR empty */
#define UART_IIR_ID_RX      0x04u  /* Received data available */
#define UART_IIR_ID_LSR     0x06u  /* Receiver line status */
#define UART_IIR_ID_RXTO    0x0Cu  /* RX timeout */

extern void console_on_char_from_irq(uint8_t ch);

static uintptr_t uart_base;
static uint32_t uart_irq;
static uint32_t uart_reg_shift;
static uint32_t uart_reg_io_width = 1;
static uint32_t uart_reg_offset;

static inline uintptr_t uart_reg_addr(uint32_t reg_index) {
  return uart_base + (uintptr_t)uart_reg_offset +
         ((uintptr_t)reg_index << uart_reg_shift);
}

static inline void uart_w(uint32_t reg_index, uint8_t v) {
  uintptr_t addr = uart_reg_addr(reg_index);
  switch (uart_reg_io_width) {
    case 4:
      *(volatile uint32_t *)addr = (uint32_t)v;
      break;
    case 2:
      *(volatile uint16_t *)addr = (uint16_t)v;
      break;
    default:
      *(volatile uint8_t *)addr = v;
      break;
  }
}

static inline uint8_t uart_r(uint32_t reg_index) {
  uintptr_t addr = uart_reg_addr(reg_index);
  switch (uart_reg_io_width) {
    case 4:
      return (uint8_t)(*(volatile uint32_t *)addr);
    case 2:
      return (uint8_t)(*(volatile uint16_t *)addr);
    default:
      return *(volatile uint8_t *)addr;
  }
}

static inline uint8_t uart_rbr_read(void) {
  return uart_r(UART_RBR);
}

static inline void uart_thr_write(uint8_t v) {
  uart_w(UART_THR, v);
}

static inline uint8_t uart_ier_read(void) {
  return uart_r(UART_IER);
}

static inline void uart_ier_write(uint8_t v) {
  uart_w(UART_IER, v);
}

static inline uint8_t uart_iir_read(void) {
  return uart_r(UART_IIR);
}

static inline uint8_t uart_lsr_read(void) {
  return uart_r(UART_LSR);
}

static inline uint8_t uart_msr_read(void) {
  return uart_r(UART_MSR);
}

uint32_t uart16550_get_irq(void) {
  return uart_irq;
}

static void uart16550_parse_dt_params(const void *fdt) {
  if (!fdt) return;

  int off = fdt_node_offset_by_compatible(fdt, -1, "ns16550a");
  if (off < 0) return;

  int len = 0;

  const fdt32_t *p = (const fdt32_t *)fdt_getprop(fdt, off, "reg-shift", &len);
  if (p && len >= (int)sizeof(fdt32_t)) {
    uart_reg_shift = fdt32_to_cpu(p[0]);
  }

  p = (const fdt32_t *)fdt_getprop(fdt, off, "reg-io-width", &len);
  if (p && len >= (int)sizeof(fdt32_t)) {
    uint32_t w = fdt32_to_cpu(p[0]);
    if (w == 1 || w == 2 || w == 4) {
      uart_reg_io_width = w;
    }
  }

  p = (const fdt32_t *)fdt_getprop(fdt, off, "reg-offset", &len);
  if (p && len >= (int)sizeof(fdt32_t)) {
    uart_reg_offset = fdt32_to_cpu(p[0]);
  }
}

void uart16550_init(void) {
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

  uart_base = (uintptr_t)base;
  uart_irq  = irq;
  uart16550_parse_dt_params(fdt);

  /* baud rate setting or others */
  uart_ier_write(UART_IER_ERBFI);
}

void uart16550_irq_handler(uint32_t irq, void *arg) {
  (void)irq;
  (void)arg;

  /*
   * PLIC 是电平触发：只要 UART 还在 assert IRQ，这个 IRQ 会被不断 claim。
   * 16550 的正确处理方式是：循环读取 IIR，直到 no pending。
   */
  for (;;) {
    uint8_t iir = uart_iir_read();

    if (iir & UART_IIR_NO_PENDING) {
      break;  /* 没有更多 UART 内部中断源 */
    }

    /* pr_debug("uart irq: hart%u iir=0x%02x ier=0x%02x lsr=0x%02x", */
    /*          cpu_current_hartid(), iir, uart_ier_read(), uart_lsr_read()); */

    switch (iir & UART_IIR_ID_MASK) {
      case UART_IIR_ID_RX:    /* RX data available */
      case UART_IIR_ID_RXTO:  /* RX timeout */
        /*
         * RX 类中断：把 FIFO/接收缓冲读空即可清 pending。
         * 注意：要用 LSR.DR 驱动读取，避免丢字节。
         */
        while (uart_lsr_read() & UART_LSR_DR) {
          uint8_t ch = uart_rbr_read();
          console_on_char_from_irq(ch);
        }
        break;

      case UART_IIR_ID_LSR:
        /*
         * Line Status：必须读 LSR 清它，否则会导致重复中断。
         * 这里你也可以解析 framing/parity/overrun 等错误位做统计。
         */
        (void)uart_lsr_read();
        break;

      case UART_IIR_ID_MSR:
        /*
         * Modem Status：必须读 MSR 清它。
         * QEMU 下通常不会用到，但不清会反复进中断。
         */
        (void)uart_msr_read();
        break;

      case UART_IIR_ID_THRE:
        /*
         * THRE：只有在你实现“TX 中断驱动发送”时才需要处理。
         * 如果你现在是 polling 写串口，强烈建议 init 时关闭 IER.ETBEI，
         * 这样就不会因为 log/echo 引起额外中断。
         */
        /* 可选：如果你不做 TX IRQ，啥也不干；更好的是确保 ETBEI 被禁用。 */
        break;

      default:
        /*
         * 理论上不会到这里。到这里也不要 panic，
         * 读一次 LSR/MSR 尽量清掉潜在源，避免死循环。
         */
        (void)uart_lsr_read();
        (void)uart_msr_read();
        break;
    }
  }
}

void uart16550_putc(char c) {
  /* 等待 TX 空 */
  while ((uart_lsr_read() & UART_LSR_THRE) == 0) {
    /* spin */
  }
  uart_thr_write((uint8_t)c);
}

void uart16550_write(const char *buf, size_t len) {
  if (!buf || len == 0) return;

  for (size_t i = 0; i < len; ++i) {
    char c = buf[i];
    if (c == '\n') uart16550_putc('\r');
    uart16550_putc(c);
  }
}

void uart16550_puts(const char *s) {
  if (!s) return;

  while (*s) {
    if (*s == '\n') uart16550_putc('\r');
    uart16550_putc(*s++);
  }
}

void uart16550_put_hex64(uint64_t x) {
  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (x >> i) & 0xF;
    char c      = (nib < 10) ? ('0' + nib) : ('a' + (nib - 10));
    uart16550_putc(c);
  }
}
