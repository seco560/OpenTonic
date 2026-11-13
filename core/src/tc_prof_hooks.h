/* Private to the core's .c files: the zones and counters of tc_prof.h. Off (no clock installed) each is one branch.
 *   double t = TC_PROF_BEGIN();  ...  TC_PROF_END(TC_PZ_..., t);
 *   TC_PROF_COUNT(TC_PC_..., n); */
#ifndef TONIC_TC_PROF_HOOKS_H
#define TONIC_TC_PROF_HOOKS_H
#include "tonic/tc_prof.h"

extern bool tc_prof_on;
double tc_prof_now(void);
void tc_prof_end(tc_prof_zone z, double t0);
void tc_prof_count(tc_prof_counter c, uint64_t n);

#define TC_PROF_BEGIN() (tc_prof_on ? tc_prof_now() : 0.0)
#define TC_PROF_END(z, t0) do { if (tc_prof_on) tc_prof_end((z), (t0)); } while (0)
#define TC_PROF_COUNT(c, n) do { if (tc_prof_on) tc_prof_count((c), (uint64_t)(n)); } while (0)
#endif
