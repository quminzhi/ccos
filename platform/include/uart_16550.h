#ifndef BAREMETAL_UART_16550_H
#define BAREMETAL_UART_16550_H

// platform/include/uart_16550.h
#pragma once

#include <stddef.h>
#include <stdint.h>

/* QEMU virt 上的 16550 兼容 UART 驱动接口 */

void uart16550_init(void);                 /* 当前可以是空实现，占个位 */
void uart16550_putc(char c);
void uart16550_write(const char *buf, size_t len);
void uart16550_puts(const char *s);
void uart16550_put_hex64(uint64_t x);

#endif //BAREMETAL_UART_16550_H
