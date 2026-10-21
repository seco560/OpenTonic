"""Decoder for Tonic Trouble .gf textures (after CNT XOR is removed).

Header (14 bytes, little-endian):
  u32 format   565 / 1555 / 4444 (16-bit RGB layouts) or 88 (8-bit intensity + 8-bit alpha)
  u32 width, u32 height
  u8  channels (bytes per pixel), u8 repeat_byte
Pixel data is stored planar (one plane per byte of the pixel, low byte first) and
RLE-compressed: repeat_byte, value, count expands to count copies of value;
any other byte is a literal.
Rows are stored bottom-up (TGA's default origin); decode() returns them top-down.
Engine UVs address GF rows directly, so v=0 is the bottom row of the upright image.
"""
import struct
import zlib


def decode(buf):
    fmt, w, h, ch, rep = struct.unpack_from('<IIIBB', buf, 0)
    n = w * h * ch
    out = bytearray()
    i = 14
    while len(out) < n and i < len(buf):
        b = buf[i]
        if b == rep:
            out += bytes([buf[i + 1]]) * buf[i + 2]
            i += 3
        else:
            out.append(b)
            i += 1
    if len(out) != n:
        raise ValueError(f'RLE stream size mismatch: {len(out)}/{n}')
    plane = w * h
    rgba = bytearray(plane * 4)
    for p in range(plane):
        lo = out[p]
        hi = out[plane + p] if ch > 1 else 0
        v = lo | hi << 8
        if fmt == 565:
            r, g, b, a = (v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255
        elif fmt == 1555:
            r, g, b, a = (v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255 if v >> 15 else 0
        elif fmt == 4444:
            a, r, g, b = (v >> 12) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17
        elif fmt == 88:
            r = g = b = lo
            a = hi
        else:
            raise ValueError(f'unknown gf format {fmt}')
        rgba[p * 4:p * 4 + 4] = bytes((r, g, b, a))
    stride = w * 4
    return w, h, b''.join(rgba[y * stride:(y + 1) * stride] for y in reversed(range(h)))


def to_png(w, h, rgba):
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, body):
        return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body))

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
