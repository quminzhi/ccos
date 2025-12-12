#pragma once

#include "types.h"

/*
 * Start a shell thread:
 *   - Success: return tid
 *   - Failure: return negative error
 *
 * Typical use in user_main:
 *   tid_t shell_tid = shell_start();
 *   thread_join(shell_tid, &status);
 */
tid_t shell_start(void);

/*
 * Entry function for the shell thread.
 * It can be passed directly to thread_create:
 *
 *   thread_create(shell_thread, NULL, "shell");
 */
void shell_thread(void *arg) __attribute__((noreturn));
