// lib/limits.h  - minimal limits for bare-metal
#pragma once

// 这里假设 int 是 32 位（在 RISC-V RV64/RV32 上都成立）
// 用 0x7fffffff 避免和 long 的宽度混淆
#ifndef INT_MAX
#define INT_MAX 0x7fffffff
#endif

#ifndef INT_MIN
#define INT_MIN (-INT_MAX - 1)
#endif

// 不是必须，但顺手补一下也行
#ifndef UINT_MAX
#define UINT_MAX 0xffffffffu
#endif
