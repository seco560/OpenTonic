#include "tc_internal.h"
#include "tonic/tc_alloc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *default_alloc(size_t size, void *user) { (void)user; return malloc(size); }
static void default_free(void *ptr, void *user) { (void)user; free(ptr); }

static tc_allocator current = {default_alloc, default_free, NULL};

void tc_set_allocator(const tc_allocator *allocator) {
    if (allocator && allocator->alloc && allocator->free) current = *allocator;
    else { current.alloc = default_alloc; current.free = default_free; current.user = NULL; }
}

void *tc_alloc(size_t size) { return size ? current.alloc(size, current.user) : NULL; }

void *tc_calloc(size_t count, size_t size) {
    void *p;
    if (count == 0 || size == 0 || count > SIZE_MAX / size) return NULL;
    p = current.alloc(count * size, current.user);
    if (p) memset(p, 0, count * size);
    return p;
}

void tc_free(void *ptr) { if (ptr) current.free(ptr, current.user); }
