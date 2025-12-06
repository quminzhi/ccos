#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* 内核和 syscall 用的 console API */
void console_init(void);
void console_write(const char *buf, size_t len);

/* 用户态 read() 使用的阻塞读接口 */
int  console_read_blocking(char *buf, size_t len);

/* UART IRQ 回调入口：在中断上下文里被调用 */
void console_on_char_from_irq(uint8_t ch);

#endif // CONSOLE_H
