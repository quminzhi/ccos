#pragma once

#include "uthread.h"

/*
 * 启动一个 shell 线程：
 *   - 成功：返回 tid
 *   - 失败：返回负数
 *
 * 通常在 user_main 里这样用：
 *   tid_t shell_tid = shell_start();
 *   thread_join(shell_tid, &status);
 */
tid_t shell_start(void);

/*
 * shell 线程入口函数。
 * 也可以直接传给 thread_create：
 *
 *   thread_create(shell_thread, NULL, "shell");
 */
void shell_thread(void *arg) __attribute__((noreturn));
