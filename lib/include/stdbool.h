// lib/include/stdbool.h
#pragma once

#ifndef __cplusplus

/* C23 及之后: bool/true/false 都是关键字，不能再 typedef / #define */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L

/* 什么都不用做，直接用语言内置的 bool/true/false 即可 */

#else /* C99/C11/C17 等老标准 */

typedef _Bool bool;
#define true                          1
#define false                         0
#define __bool_true_false_are_defined 1

#endif

#endif /* !__cplusplus */
