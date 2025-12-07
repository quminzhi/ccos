#pragma once

#include <stdint.h>

#define CLOCK_REALTIME 0

struct timespec {
    uint64_t tv_sec;
    uint32_t tv_nsec;
};
