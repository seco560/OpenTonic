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

/* The chunk container both files use (tools/ttre/chunks.py): char magic[4], u32 TC_CONTAINER, then chunks to the end
 * of the file, { char tag[4], u32 size, size bytes, zero padding to 4 }. No versions: a loader asks for the chunks it
 * needs and reads each to its exact end. A missing chunk, or one of another size than the loader reads, means the file
 * came from another converter: TC_ERR_STALE. Tags the loader does not ask for are skipped. */
#define TC_CONTAINER 100u /* the numbered formats before it had 1-12 here */
typedef struct chunks { const uint8_t *data; size_t size; } chunks;

/* Next chunk at *o (header included); 0 at the end. The framing was checked by chunks_open. */
static inline int chunk_next(const chunks *c, size_t *o, const uint8_t **tag, reader *body) {
    uint32_t n;
    if (c->size - *o < 8) return 0;
    n = le32(c->data + *o + 4);
    *tag = c->data + *o;
    body->data = c->data + *o + 8;
    body->size = n;
    body->offset = 0;
    body->error = TC_OK;
    *o += 8 + (size_t)n;
    *o = (4 - n % 4) % 4 > c->size - *o ? c->size : *o + (4 - n % 4) % 4;
    return 1;
}
static inline tc_result chunks_open(chunks *c, const uint8_t *data, size_t size, const char magic[4]) {
    size_t o = 8;
    const uint8_t *tag;
    reader body;
    if (!data || size < 8 || memcmp(data, magic, 4) != 0) return TC_ERR_BAD_MAGIC;
    if (le32(data + 4) != TC_CONTAINER) return TC_ERR_STALE;
    c->data = data;
    c->size = size;
    while (size - o >= 8)
        if (le32(data + o + 4) > size - o - 8) return TC_ERR_TRUNCATED;
        else chunk_next(c, &o, &tag, &body);
    return o == size ? TC_OK : TC_ERR_TRUNCATED;
}
/* *r := a reader over the first chunk called tag. Missing: r reads nothing and carries TC_ERR_STALE, so the loader
 * can go on reading (zeros, NULL arrays) and learn it from chunk_end. */
static inline int chunk_need(const chunks *c, const char tag[4], reader *r) {
    size_t o = 8;
    const uint8_t *t;
    while (chunk_next(c, &o, &t, r))
        if (memcmp(t, tag, 4) == 0) return 1;
    r->data = NULL;
    r->size = r->offset = 0;
    r->error = TC_ERR_STALE;
    return 0;
}
/* After reading a chunk: TC_OK when the loader read it exactly; reading past its end or stopping short is a layout
 * mismatch (TC_ERR_STALE); out of memory stays itself. */
static inline tc_result chunk_end(const reader *r) {
    if (r->error == TC_ERR_OUT_OF_MEMORY) return r->error;
    return r->error || r->offset != r->size ? TC_ERR_STALE : TC_OK;
}
/* The STMP chunk (the converter's stamp) into out, "" when missing. */
static inline void chunk_stamp(const chunks *c, char *out, size_t out_size) {
    reader r;
    uint16_t n;
    out[0] = 0;
    if (!chunk_need(c, "STMP", &r)) return;
    n = rd_u16(&r);
    if (!have(&r, n)) return;
    if (n >= out_size) n = (uint16_t)(out_size - 1);
    memcpy(out, r.data + r.offset, n);
    out[n] = 0;
}
/* CRC-32 as zlib's (the .ttai's LINK chunk holds the one of the .ttlv's COLL chunk). */
static inline uint32_t tc_crc32(const uint8_t *p, size_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                   0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                   0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ t[c & 15];
        c = (c >> 4) ^ t[c & 15];
    }
    return ~c;
}
/* tc_sector.c: the .ttlv SECT chunk, after MESH and PRSO (its ranges and perso sectors follow their counts). */
tc_result tc_sectors_read(tc_sectors *s, reader *r, const tc_mesh *meshes, uint32_t mesh_count, uint32_t perso_count);
void tc_sectors_free(tc_sectors *s);
#endif
