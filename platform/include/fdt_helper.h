// fdt_helper.h
#pragma once
#include <stdint.h>

int fdt_find_reg_by_compat(const void *fdt, const char *compat, uint64_t *base,
                           uint64_t *size);

int fdt_find_irq_by_compat(const void *fdt, const char *compat, uint32_t *irq);
