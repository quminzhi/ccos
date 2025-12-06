#include "sysfile.h"
#include <console.h>

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

uint64_t sys_read(int fd, char *buf, uint64_t len)
{
  if (len == 0) return 0;

  switch (fd) {
    case FD_STDIN:
      /* 阻塞从 console ring buffer 里取数据 */
      return (int)console_read_blocking(buf, (size_t)len);

    default:
      return -1;
  }
}
