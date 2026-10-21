/* Private to the core's .c files. */
#ifndef TONIC_TC_INTERNAL_H
#define TONIC_TC_INTERNAL_H
/* No fused multiply-add contraction: keeps results bit-comparable with the Swift original and
 * across compilers (GCC in ISO C mode already defaults to off). */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif
#endif
