#pragma once
#include <stdint.h>

#define IRQSTAT_MAX_NAME 16
#define IRQSTAT_MAX_IRQ  64

struct irqstat_user {
  uint32_t irq;
  uint32_t _pad;
  uint64_t count;
  uint64_t first_tick;
  uint64_t last_tick;
  uint64_t max_delta;
  char name[IRQSTAT_MAX_NAME];
};
