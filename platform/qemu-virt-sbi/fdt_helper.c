// fdt_helper.c
#include "fdt_helper.h"
#include "platform.h"
#include "libfdt.h"

int fdt_find_reg_by_compat(const void *fdt, const char *compat, uint64_t *base,
                           uint64_t *size)
{
  int offset = fdt_node_offset_by_compatible(fdt, -1, compat);
  if (offset < 0) return -1;

  int len;
  const uint32_t *reg = fdt_getprop(fdt, offset, "reg", &len);
  if (!reg || len < 16) return -1;  // 64-bit addr + 64-bit size = 16 bytes

  // QEMU virt 是 64-bit cells: <hi_addr lo_addr hi_size lo_size>
  uint64_t addr_hi = fdt32_to_cpu(reg[0]);
  uint64_t addr_lo = fdt32_to_cpu(reg[1]);
  uint64_t size_hi = fdt32_to_cpu(reg[2]);
  uint64_t size_lo = fdt32_to_cpu(reg[3]);

  *base            = (addr_hi << 32) | addr_lo;
  *size            = (size_hi << 32) | size_lo;
  return 0;
}

int fdt_find_irq_by_compat(const void *fdt, const char *compat,
                           uint32_t *irq_out)
{
  int offset = fdt_node_offset_by_compatible(fdt, -1, compat);
  if (offset < 0) {
    return -1;  // 没找到这个 compatible 的节点
  }

  int len             = 0;
  const fdt32_t *intr = fdt_getprop(fdt, offset, "interrupts", &len);
  if (!intr || len < (int)sizeof(fdt32_t)) {
    // 没有 interrupts，或者长度连 1 个 cell 都不够
    return -1;
  }

  // 对 QEMU virt：PLIC #interrupt-cells = <1>，所以第一个 cell 就是 IRQ 编号
  *irq_out = fdt32_to_cpu(intr[0]);
  return 0;
}
