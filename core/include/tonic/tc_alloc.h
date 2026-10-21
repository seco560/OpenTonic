/* Allocation hook: every core allocation goes through tc_alloc/tc_free, so a console backend can
 * route them to its own heap. Default: malloc/free. */
#ifndef TONIC_TC_ALLOC_H
#define TONIC_TC_ALLOC_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tc_allocator {
    void *(*alloc)(size_t size, void *user); /* NULL on failure; must be suitably aligned for any type */
    void (*free)(void *ptr, void *user);     /* ptr may be NULL */
    void *user;
} tc_allocator;

/* Install an allocator (NULL restores malloc/free). Call before loading anything. */
void tc_set_allocator(const tc_allocator *allocator);
void *tc_alloc(size_t size);
void *tc_calloc(size_t count, size_t size); /* zeroed, overflow-checked; NULL for 0 bytes too */
void tc_free(void *ptr);

#ifdef __cplusplus
}
#endif
#endif
