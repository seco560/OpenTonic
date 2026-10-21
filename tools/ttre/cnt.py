"""Reader for OpenSpace .cnt archives (Textures.cnt, Vignette.cnt).

Layout (little-endian), as seen in Tonic Trouble retail PC:
  i32 dir_count, i32 file_count, u8 is_xor, u8 is_checksum, u8 name_key
  dir_count x { i32 len, char[len] name ^ name_key }
  u8 checksum
  file_count x { i32 dir_index, i32 len, char[len] name ^ name_key,
                 u8[4] data_key, u32 unknown, u32 offset, u32 size }
File data is XORed with data_key cyclically, except the trailing size % 4 bytes.
Cross-checked against raymap Texture/CNT.cs and BinarySerializer.OpenSpace.
"""
import struct
from dataclasses import dataclass


@dataclass
class Entry:
    path: str
    key: bytes
    unknown: int
    offset: int
    size: int


class Cnt:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        nd, nf, self.is_xor, self.is_checksum, key = struct.unpack_from('<iiBBB', d, 0)
        o = 11

        def name():
            nonlocal o
            n, = struct.unpack_from('<i', d, o)
            s = bytes(b ^ key for b in d[o + 4:o + 4 + n]).decode('latin1')
            o += 4 + n
            return s

        self.dirs = [name() for _ in range(nd)]
        self.checksum = d[o]
        o += 1
        self.entries = []
        for _ in range(nf):
            di, = struct.unpack_from('<i', d, o)
            o += 4
            fname = name()
            k, unk, off, size = struct.unpack_from('<4sIII', d, o)
            o += 16
            path = f'{self.dirs[di]}\\{fname}' if di >= 0 else fname
            self.entries.append(Entry(path, k, unk, off, size))
        self.table_end = o

    def read(self, e):
        raw = self.data[e.offset:e.offset + e.size]
        if e.key == b'\0\0\0\0':
            return raw
        k = e.key
        whole = len(raw) & ~3  # trailing size % 4 bytes are stored unencrypted
        return bytes(b ^ k[i & 3] for i, b in enumerate(raw[:whole])) + raw[whole:]

    def verify(self):
        """Return entries whose data falls outside the file or is entirely zero."""
        bad = []
        for e in self.entries:
            chunk = self.data[e.offset:e.offset + e.size]
            if len(chunk) != e.size or not chunk.strip(b'\0'):
                bad.append(e)
        return bad
