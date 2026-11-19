/* Private to the core's .c files. */
#ifndef TONIC_TC_INTERNAL_H
#define TONIC_TC_INTERNAL_H
/* No fused multiply-add contraction: keeps results bit-comparable with the Swift original and
 * across compilers (GCC in ISO C mode already defaults to off). */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif
#include <ctype.h>
#include <stddef.h>

/* Names as the data writes them: compared without case (level and place names), copied cut to fit (NULL: ""). */
static inline int tc_same_text(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}
static inline void tc_copy_text(char *dst, size_t n, const char *src) {
    size_t i;
    for (i = 0; src && src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = 0;
}
#endif
