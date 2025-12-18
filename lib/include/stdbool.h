// lib/include/stdbool.h
#pragma once

#ifndef __cplusplus

/* C23 and later: bool/true/false are keywords; no typedef/#define needed */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L

/* Nothing to do; use the language built-ins */

#else /* Older standards: C99/C11/C17 */

typedef _Bool bool;
#define true                          1
#define false                         0
#define __bool_true_false_are_defined 1

#endif

#endif /* !__cplusplus */
