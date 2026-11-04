"""Decoder for Tonic Trouble .gf textures (after CNT XOR is removed).

Header (14 bytes, little-endian):
  u32 format   565 / 1555 / 4444 (16-bit RGB layouts) or 88 (8-bit intensity + 8-bit alpha)
  u32 width, u32 height
  u8  channels (bytes per pixel), u8 repeat_byte
Pixel data is stored planar (one plane per byte of the pixel, low byte first) and
RLE-compressed: repeat_byte, value, count expands to count copies of value;
any other byte is a literal.
Rows are stored bottom-up (TGA's default origin); decode() returns them top-down.

Special Edition (1998) textures use the older header (10 bytes): u32 width, u32 height, u8 channels (3 = BGR, 4 =
BGRA, 8 bits each), u8 repeat_byte; the same planar RLE (planes in TGA byte order). Told apart by the first u32: the
retail format codes are small (88..4444), an old header's width is a power of two >= 8 with a sane height after it.
Engine UVs address GF rows directly, so v=0 is the bottom row of the upright image.
"""
import struct
import zlib


FORMATS = (565, 1555, 4444, 88)


def decode(buf):
    if struct.unpack_from('<I', buf, 0)[0] not in FORMATS:
        return decode_old(buf)
    fmt, w, h, ch, rep = struct.unpack_from('<IIIBB', buf, 0)
    out = rle(buf, 14, w * h * ch, rep)
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
    return w, h, flip(rgba, w, h)


def rle(buf, i, n, rep):
    out = bytearray()
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
    return out


def flip(rgba, w, h):
    stride = w * 4
    return b''.join(rgba[y * stride:(y + 1) * stride] for y in reversed(range(h)))


def decode_old(buf):
    """The Special Edition's header (see the module docstring): 3 planes B, G, R (+ A), 8 bits each."""
    w, h, ch, rep = struct.unpack_from('<IIBB', buf, 0)
    if ch not in (3, 4) or not 0 < w <= 4096 or not 0 < h <= 4096:
        raise ValueError(f'unknown gf header {buf[:10].hex()}')
    plane = w * h
    out = rle(buf, 10, plane * ch, rep)
    rgba = bytearray(plane * 4)
    rgba[2::4], rgba[1::4], rgba[0::4] = out[:plane], out[plane:2 * plane], out[2 * plane:3 * plane]
    rgba[3::4] = out[3 * plane:] if ch == 4 else b'\xff' * plane
    return w, h, flip(rgba, w, h)


def to_png(w, h, rgba):
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, body):
        return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body))

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
