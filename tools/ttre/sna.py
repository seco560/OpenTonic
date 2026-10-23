"""Tonic Trouble level memory snapshots (.sna) and relocation tables (.rtb/.rtp/.rtt).

All of these are encrypted with a rolling 10-byte window key (see decrypt()).

SNA (after decryption):
  u8 header_len, header_len bytes (obfuscated path string)
  blocks until EOF:
    u8 module, u8 id, i32 base_in_memory   (no unk byte here, unlike Rayman 2)
    if base != -1: u32 unk2, u32 unk3, u32 max_pos_minus_9, u32 size, u8 unk_tt, size bytes
  A block is a dump of engine memory that lived at base_in_memory..base+size.

Relocation table:
  u8 count
  count x { u8 module, u8 id, u32 n, n x { u32 address, u8 target_module, u8 target_id } }
  Each entry says: the u32 stored at `address` (inside block module/id) is a pointer
  into block target_module/target_id.

Pointer files (.gpt global pointer table, .ptx texture table): same encryption, no header,
just engine data that the loader keeps outside any block. The fix ones are relocated by
.rtp/.rtt, whose single list (keyed (4,0)) has the same entry format but `address` holds the
pointer *value*; entries match the file's dwords in order (pointer k is the next dword equal
to entry k). Level .gpt/.ptx have no relocation table: their pointers are plain addresses in
the level's memory view (block bases from level.sna), resolved on demand (see Level.ptr).
"""
import os
import struct
from dataclasses import dataclass, field

KEY = bytes.fromhex('4159BEC70D991CA3753F')


def decrypt(buf):
    k = bytearray(KEY)
    out = bytearray(len(buf))
    for i, c in enumerate(buf):
        j = i % 10
        out[i] = c ^ k[j]
        k[j] = (KEY[j] + c) & 0xFF
    return bytes(out)


def find_file(path):
    """The existing file that matches `path` ignoring case, component by component (the installed game mixes case,
    e.g. LEVELS/Land/land.sna; macOS doesn't mind, Linux does). Unmatched paths come back as given, so open() reports
    them."""
    if os.path.exists(path):
        return path
    head, tail = os.path.split(path)
    if head and not os.path.exists(head):
        head = find_file(head)
    try:
        for entry in os.listdir(head or '.'):
            if entry.lower() == tail.lower():
                return os.path.join(head, entry)
    except OSError:
        pass
    return os.path.join(head, tail)


@dataclass
class Block:
    module: int
    id: int
    base: int
    size: int = 0
    data_pos: int = 0
    extra: tuple = ()


@dataclass(eq=False)  # identity-hashed: used as a dict key
class Sna:
    data: bytes = field(repr=False)
    header: bytes
    blocks: list = field(default_factory=list)

    @classmethod
    def load(cls, path):
        d = decrypt(open(find_file(path), 'rb').read())
        hl = d[0]
        s = cls(d, d[1:1 + hl])
        o = 1 + hl
        while o < len(d):
            mod, bid, base = struct.unpack_from('<BBi', d, o)
            o += 6
            b = Block(mod, bid, base)
            if base != -1:
                u2, u3, maxpos, size, ut = struct.unpack_from('<IIIIB', d, o)
                o += 17
                b.size, b.data_pos, b.extra = size, o, (u2, u3, maxpos, ut)
                o += size
            s.blocks.append(b)
        if o != len(d):
            raise ValueError(f'block walk overran file: {o} != {len(d)}')
        return s

    @classmethod
    def pointer_file(cls, path):
        """Load a .gpt/.ptx as a pseudo-snapshot with one block covering the whole file."""
        d = decrypt(open(find_file(path), 'rb').read())
        return cls(d, b'', [Block(-1, -1, 0, len(d), 0)])

    def block(self, module, bid):
        found = [b for b in self.blocks if (b.module, b.id) == (module, bid) and b.size]
        return found[-1] if found else None


def load_relocations(path):
    d = decrypt(open(find_file(path), 'rb').read())
    count = d[0]
    o = 1
    lists = {}
    for _ in range(count):
        mod, bid, n = struct.unpack_from('<BBI', d, o)
        o += 6
        ptrs = [struct.unpack_from('<IBB', d, o + 6 * i) for i in range(n)]
        o += 6 * n
        lists.setdefault((mod, bid), []).extend(ptrs)
    if o != len(d):
        raise ValueError(f'relocation table size mismatch: {o} != {len(d)}')
    return lists


NO_RELOC = (0xFF, 0xFF)  # TT marks pointers that are not relocated this way; raymap skips them too


class Level:
    """Fix + level snapshots with relocations applied: maps engine addresses to file data.

    Each pointer is resolved against the *level's* view of the target block (its base),
    falling back to fix data when the level only carries an empty placeholder.
    Also loads fix/level .gpt and .ptx (relocated via fix.rtp/.rtt, resp. by address).
    Locations are (sna, file offset) tuples; follow pointers with ptr().
    """

    def __init__(self, levels_dir, name):
        self.name = name
        self.fix = Sna.load(f'{levels_dir}/fix.sna')
        self.lvl = Sna.load(f'{levels_dir}/{name}/{name}.sna')
        self.fix_gpt = Sna.pointer_file(f'{levels_dir}/fix.gpt')
        self.fix_ptx = Sna.pointer_file(f'{levels_dir}/fix.ptx')
        self.gpt = Sna.pointer_file(f'{levels_dir}/{name}/{name}.gpt')
        self.ptx = Sna.pointer_file(f'{levels_dir}/{name}/{name}.ptx')
        self.unrelocated = (self.gpt, self.ptx)  # no .rtp/.rtt for levels in TT
        self.relocs = {}
        for f in (f'{levels_dir}/fix.rtb', f'{levels_dir}/{name}/FixLvl.rtb', f'{levels_dir}/{name}/{name}.rtb'):
            for k, v in load_relocations(f).items():
                self.relocs.setdefault(k, []).extend(v)
        self.pointers = {}  # (sna, file offset of pointer) -> (sna, file offset of target)
        self.unresolved = []
        for (m, i), ptrs in self.relocs.items():
            for addr, tm, ti in ptrs:
                if (tm, ti) == NO_RELOC:
                    continue
                src, tgt = self.find(m, i), self.find(tm, ti)
                where = self.locate(src, addr) if src else None
                if where is None or tgt is None:
                    self.unresolved.append((m, i, addr, tm, ti))
                    continue
                v, = struct.unpack_from('<I', where[0].data, where[1])
                dest = self.locate(tgt, v)
                if dest is None:
                    self.unresolved.append((m, i, addr, tm, ti))
                    continue
                self.pointers[where] = dest
        for pf, rt in ((self.fix_gpt, 'fix.rtp'), (self.fix_ptx, 'fix.rtt')):
            self._relocate_pointer_file(pf, load_relocations(f'{levels_dir}/{rt}'))
        # level-view address space: each level.sna block entry (data or placeholder) gives the base
        self.view = sorted((b.base, b.module, b.id) for b in self.lvl.blocks if b.base != -1)

    def _relocate_pointer_file(self, pf, relocs):
        entries = [e for v in relocs.values() for e in v]
        k = 0
        for off in range(0, len(pf.data) - 3, 4):
            if k == len(entries):
                break
            v, = struct.unpack_from('<I', pf.data, off)
            value, tm, ti = entries[k]
            if v != value:
                continue
            k += 1
            tgt = self.find(tm, ti)
            dest = self.locate(tgt, v) if tgt else None
            if dest is None:
                self.unresolved.append((-1, -1, off, tm, ti))
            else:
                self.pointers[(pf, off)] = dest
        if k != len(entries):
            raise ValueError(f'pointer file relocation matched {k}/{len(entries)} entries')

    def resolve(self, addr):
        """Engine address in the level's memory view -> (sna, offset), or None."""
        for base, m, i in reversed(self.view):
            if addr >= base:
                sb = self.find(m, i)
                return self.locate(sb, addr) if sb else None
        return None

    def ptr(self, loc, off=0):
        """Follow the pointer stored at loc+off; None for null/unrelocated pointers."""
        s, o = loc[0], loc[1] + off
        p = self.pointers.get((s, o))
        if p is None and s in self.unrelocated:
            v, = struct.unpack_from('<I', s.data, o)
            p = self.resolve(v) if v else None
        return p

    def find(self, module, bid):
        for s in (self.lvl, self.fix):
            b = s.block(module, bid)
            if b:
                return s, b
        return None

    @staticmethod
    def locate(sb, addr):
        s, b = sb
        off = addr - b.base
        return (s, b.data_pos + off) if 0 <= off <= b.size else None
