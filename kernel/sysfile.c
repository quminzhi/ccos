#include "sysfile.h"
#include <console.h>
#include "syscall.h"
#include "thread.h"

extern tid_t g_stdin_waiter;

uint64_t sys_write(int fd, const char *buf, uint64_t len)
{
  if (len == 0) return 0;

  switch (fd) {
    case FD_STDOUT:
    case FD_STDERR:
      console_write(buf, (size_t)len);
      return len;

    default:
      return -1;
  }
}

uint64_t sys_read(int fd, char *buf, uint64_t len, struct trapframe *tf,
                  int *is_non_block_read)
{
  if (fd != FD_STDIN) {
    // support stdin only
    *is_non_block_read = 1;
    return -1;
  }

  /* 先尝试非阻塞读取 */
  int n = console_read_nonblock(buf, (size_t)len);
  if (n > 0) {
    *is_non_block_read = 1;
    return (long)n;
  }

  // wait for stdin and block and waked by irq from uart (interrupt context)
  thread_wait_for_stdin(buf, len, tf);

  /* 对当前线程来说，上面的 thread_block 不会再回来。
   * sys_read 返回的这个 ret 只会被“当时 schedule 选中的那个线程”看到，
   * 但那个线程并不是当前 fd=0 的 read 调用者，所以这个返回值其实可以随便给。
   */
  return 0;
}
