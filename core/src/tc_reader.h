/* Private to the core: bounds-checked little-endian reader shared by the file loaders. */
#ifndef TONIC_TC_READER_H
#define TONIC_TC_READER_H
#include "tonic/tc_level.h"
#include "tonic/tc_alloc.h"
#include <string.h>

/* Bounds-checked little-endian reader. Every read goes through bytes, never through casts. */
typedef struct reader {
    const uint8_t *data;
    size_t size, offset;
    tc_result error;
} reader;

static inline int have(reader *r, size_t n) {
    if (r->error) return 0;
    if (n > r->size - r->offset) { r->error = TC_ERR_TRUNCATED; return 0; }
    return 1;
}
static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t rd_u32(reader *r) {
    uint32_t v;
    if (!have(r, 4)) return 0;
    v = le32(r->data + r->offset);
    r->offset += 4;
    return v;
}
static inline uint16_t rd_u16(reader *r) {
    uint16_t v;
    if (!have(r, 2)) return 0;
    v = (uint16_t)(r->data[r->offset] | (r->data[r->offset + 1] << 8));
    r->offset += 2;
    return v;
}
static inline uint8_t rd_u8(reader *r) { return have(r, 1) ? r->data[r->offset++] : 0; }
static inline float bits_to_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline float rd_f32(reader *r) { return bits_to_float(rd_u32(r)); }
static inline int32_t rd_i32(reader *r) { uint32_t u = rd_u32(r); int32_t v; memcpy(&v, &u, 4); return v; }
static inline int16_t rd_i16(reader *r) { uint16_t u = rd_u16(r); int16_t v; memcpy(&v, &u, 2); return v; }

/* Zeroed array of count elements, refusing counts the remaining bytes can't hold
 * (min_bytes_each = smallest on-disk size of one element), so corrupt counts don't allocate GBs. */
static inline void *rd_array(reader *r, size_t count, size_t elem, size_t min_bytes_each) {
    void *p;
    if (r->error || count == 0) return NULL;
    if (min_bytes_each && count > (r->size - r->offset) / min_bytes_each) { r->error = TC_ERR_TRUNCATED; return NULL; }
    p = tc_calloc(count, elem);
    if (!p) r->error = TC_ERR_OUT_OF_MEMORY;
    return p;
}
static inline char *rd_string(reader *r) {
    uint16_t len = rd_u16(r);
    char *s;
    if (!have(r, len)) return NULL;
    s = (char *)tc_alloc((size_t)len + 1);
    if (!s) { r->error = TC_ERR_OUT_OF_MEMORY; return NULL; }
    memcpy(s, r->data + r->offset, len);
    s[len] = 0;
    r->offset += len;
    return s;
}
#endif
