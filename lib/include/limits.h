// lib/limits.h  - minimal limits for bare-metal
#pragma once

// Assume int is 32-bit (true on RISC-V RV64/RV32)
// Use 0x7fffffff to avoid width confusion with long
#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif

#ifndef INT_MIN
#define INT_MIN (-INT_MAX - 1)
#endif

// Optional but convenient to define
#ifndef UINT_MAX
#define UINT_MAX 0xffffffffu
#endif
