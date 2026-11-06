"""Ed's sound effects: the fix bank's events, extracted for the viewer (docs/NOTES.md "Sound events").

Sound banks (WORLD/SOUND/Bnk_<n>.bnm, after decoding nothing: plain little-endian):
  header { u32 version (0), u32 events_offset, u32 n_events, u32 resources_offset, u32 n_resources, u32 data_offset,
           ... }
  event (0x20) { u32 id, u32 kind (1 play, 2 stop), u32 target (play: the resource index; stop: the index of the event
                 it stops), ... }
  resource (0x5c) { u32 id, u32 type (1 audio; 8 / 10 tiny raw waveforms, unused here), u32, u32 size,
                    u32 data offset, u32 volume (0x7f full), ..., file name }
The engine's global sound events are **Bnk_0's events** (84; the fix bank): the snapshot stores them as pointers
0x9bdfe0c + 0x20 x index, listed by fix.snd as { u32 index, u32 address } pairs (fix.rts relocates them). An event
plays the resource with its id; event 49 (0x00390000) has none: it is the footstep switch over the 0x0039xxxx
resources by ground material (EsCarpet ... EsEarthS, Edpasgen; its resource r2e is a type-8 table); here it plays the
generic step. An event that a stop event targets is a loop (flight: 77 played, 76 stops it).

The levels' own sounds (2026-11-02): each level's .snd (encrypted) lists { u32 event id, u32 address } for every place in its
memory holding a sound reference (script T34 nodes, animation sound events); the ids are unique over all banks, so the
event's bank follows. The core reports them as LEVEL_SOUND + the event's rank (level_events); files <bank>_<name>.wav.
Output: files/out/audio/sfx/<name>.wav (vgmstream-cli; bnm stream names carry the resource index) and events.tsv:
  index <tab> play|loop|stop|voice <tab> file (relative to sfx/, "" = silent) <tab> volume 0..1 <tab> stop: the event it stops
Usage: python3 -m tools.ttre.sfx [out_dir]
"""
import os
import struct
import subprocess
import sys

SOUND = 'files/sources/GAMEDATA/WORLD/SOUND'
EVENT_BASE = 0x9bdfe0c  # where the snapshot's Bnk_0 event 0 lived: pointer = base + 0x20 x index
FOOTSTEPS = 0x00390000
GENERIC_STEP = 0x0039001c  # Edpasgen.wav
STEP_SOUND = 256  # the core's events for the material steps (TC_AI_STEP_SOUND + floor sound type)


LEVEL_SOUND = 1024  # the core's events for the levels' own banks: LEVEL_SOUND + rank of the event (level_events)
LEVELS = 'files/sources/GAMEDATA/WORLD/LEVELS'


def level_events():
    """Every event of the banks other than Bnk_0, as {event id: (global index, bank, k, kind, target)}: index =
    LEVEL_SOUND + its rank in (bank, k) order. Event ids are unique over all 86 banks (checked 2026-11-02)."""
    import glob
    if not hasattr(level_events, 'cache'):
        rows = []
        for path in glob.glob(f'{SOUND}/Bnk_*.bnm'):
            bank = int(os.path.basename(path)[4:-4])
            if bank == 0:
                continue
            try:
                events, _ = read_bank(path)
            except (struct.error, OSError):
                continue
            rows += [(bank, k, eid, kind, target) for k, (eid, kind, target) in enumerate(events)]
        rows.sort()
        level_events.cache = {eid: (LEVEL_SOUND + i, bank, k, kind, target) for i, (bank, k, eid, kind, target) in enumerate(rows)}
    return level_events.cache


def level_snd(level, levels_dir=LEVELS):
    """A level's .snd (encrypted as the snapshots): { u32 event id, u32 address } pairs, one per place in the level's
    memory that holds a sound event reference (a script's T34 node, an animation's sound event): {address: id}."""
    import glob
    from . import sna
    files = glob.glob(f'{levels_dir}/{level}/*.snd')
    if not files:
        return {}
    b = sna.decrypt(open(files[0], 'rb').read())
    w = struct.unpack_from(f'<{len(b) // 4}I', b)
    return {w[i + 1]: w[i] for i in range(0, len(w) - 1, 2)}


def mem_address(loc):
    """A snapshot location (sna, file offset) -> its address in the game's memory (the block's base), or None."""
    s, off = loc
    for b in s.blocks:
        if b.size and b.data_pos <= off < b.data_pos + b.size:
            return b.base + off - b.data_pos
    return None


def sound_at(level_table, loc, raw):
    """The core's event index of the sound reference stored at loc (raw: its snapshot value): a Bnk_0 event by its
    pointer, else the level's own by the .snd table and level_events; None when neither knows it."""
    k = event_index(raw)
    if k is not None:
        return k
    a = mem_address(loc)
    eid = level_table.get(a) if a is not None else None
    e = level_events().get(eid) if eid is not None else None
    return e[0] if e else None


def read_bank(path):
    """(events [(id, kind, word2)], resources {id: (index, type, volume, name)})."""
    d = open(path, 'rb').read()
    _, ev_off, n_ev, res_off, n_res = struct.unpack_from('<5I', d, 0)
    events = [struct.unpack_from('<3I', d, ev_off + 0x20 * k) for k in range(n_ev)]
    resources = {}
    for r in range(n_res):
        o = res_off + 0x5c * r
        rid, typ, _, _, _, vol = struct.unpack_from('<6I', d, o)
        names = [s for s in d[o + 0x18:o + 0x5c].split(b'\0') if len(s) > 3 and all(32 <= c < 127 for c in s)]
        resources[rid] = (r, typ, vol, names[-1].decode() if names else '')
    return events, resources


def event_index(raw):
    """A snapshot sound-event pointer -> global event index, or None."""
    k, m = divmod(raw - EVENT_BASE, 0x20)
    return k if m == 0 and 0 <= k < 256 else None


def stream_index(bank):
    """Resource index -> vgmstream subsong (the bnm stream name is bnm/<resource index>/<type>-<id>/<file>)."""
    import json
    out, s, total = {}, 1, 1
    while s <= total:
        r = subprocess.run(['vgmstream-cli', '-m', '-I', '-s', str(s), bank], capture_output=True, text=True)
        line = next((l for l in r.stdout.splitlines() if l.startswith('{')), None)
        if not line:
            break
        j = json.loads(line)
        total = j.get('streamInfo', {}).get('total', total)
        name = j.get('streamInfo', {}).get('name', '')
        parts = name.split('/')
        if len(parts) >= 2 and parts[1].isdigit():
            out[int(parts[1])] = s
        s += 1
    return out


def main(out_dir='files/out/audio/sfx'):
    bank = f'{SOUND}/Bnk_0.bnm'
    events, resources = read_bank(bank)
    by_index = {v[0]: (rid, v) for rid, v in resources.items()}
    streams = stream_index(bank)
    os.makedirs(out_dir, exist_ok=True)
    rows, extracted = [], {}
    loops = {target for _, kind, target in events if kind == 2}
    for k, (eid, kind, target) in enumerate(events):
        if kind == 2:
            rows.append((k, 'stop', '', 1.0, target))
            continue
        if eid == FOOTSTEPS:
            rid, res = GENERIC_STEP, resources.get(GENERIC_STEP)
        else:
            rid, res = by_index.get(target, (None, None))  # the resource index (agrees with the id where both exist)
        file = ''
        if res and res[1] == 1 and res[0] in streams:
            file = os.path.splitext(res[3].lower())[0] + '.wav'
            if file not in extracted:
                subprocess.run(['vgmstream-cli', '-s', str(streams[res[0]]), '-o', os.path.join(out_dir, file), bank],
                               check=True, capture_output=True)
                extracted[file] = True
        rows.append((k, 'loop' if k in loops else 'play', file, (res[2] if res else 127) / 127, ''))
    # The footstep switch's sounds (docs/NOTES.md "Footsteps"): step id 0x390002 + v for the floor's sound type v
    # (GameMaterial +8; 1 EsEarthS .. 20 EsSnow, 21 EsIce, 23 EsCarpet), reported by the core as event 256 + v.
    for v in range(1, 24):
        res = resources.get(FOOTSTEPS + 2 + v)
        file = ''
        if res and res[1] == 1 and res[0] in streams:
            file = os.path.splitext(res[3].lower())[0] + '.wav'
            if file not in extracted:
                subprocess.run(['vgmstream-cli', '-s', str(streams[res[0]]), '-o', os.path.join(out_dir, file), bank],
                               check=True, capture_output=True)
                extracted[file] = True
        rows.append((STEP_SOUND + v, 'play', file, (res[2] if res else 127) / 127, ''))
    # The levels' own sounds (their banks' events that some level's .snd names): event LEVEL_SOUND + rank.
    import glob
    named = set()
    for f in glob.glob(f'{LEVELS}/*/*.snd'):
        from . import sna
        b = sna.decrypt(open(f, 'rb').read())
        named |= set(struct.unpack_from(f'<{len(b) // 4}I', b)[0::2])
    table = level_events()
    banks = {}
    for eid in sorted(named):
        if eid in table:
            banks.setdefault(table[eid][1], []).append(eid)
    level_rows = 0
    for bank, eids in sorted(banks.items()):
        path = f'{SOUND}/Bnk_{bank}.bnm'
        events, res_by_id = read_bank(path)
        by_index = {v[0]: (rid, v) for rid, v in res_by_id.items()}
        streams = stream_index(path)
        loops_b = {target for _, kind, target in events if kind == 2}
        for eid in eids:
            index, _, k, kind, target = table[eid]
            if kind == 2:  # stop: the event it stops, by its index in this bank
                stopped = events[target][0] if target < len(events) else None
                rows.append((index, 'stop', '', 1.0, table[stopped][0] if stopped in table else ''))
                level_rows += 1
                continue
            rid, res = by_index.get(target, (None, None))
            if res is None:
                res = res_by_id.get(eid)
            file = ''
            if res and res[1] == 1 and res[0] in streams:
                file = f'{bank}_' + os.path.splitext(res[3].lower())[0] + '.wav'
                if file not in extracted:
                    subprocess.run(['vgmstream-cli', '-s', str(streams[res[0]]), '-o', os.path.join(out_dir, file), path],
                                   check=True, capture_output=True)
                    extracted[file] = True
            rows.append((index, 'loop' if k in loops_b else 'play', file, (res[2] if res else 127) / 127, ''))
            level_rows += 1
    print(f'levels\' own sounds: {level_rows} events from {len(banks)} banks')
    # The voices (speech.py): event VOICE_SOUND + rank; the frontends decode one when it is said, and stop it when the
    # line is dismissed (the core's TC_AI_VOICE_STOP).
    from . import speech
    banks_of, voice_streams = speech._banks(), {}
    for (bank, index), (event, name) in sorted(speech.voices().items()):
        path = banks_of[bank][0]
        if bank not in voice_streams:
            voice_streams[bank] = stream_index(path)
        file = ''
        if index in voice_streams[bank]:
            file = f'{bank}_' + os.path.splitext(name.lower())[0] + '.wav'
            if file not in extracted:
                subprocess.run(['vgmstream-cli', '-s', str(voice_streams[bank][index]), '-o', os.path.join(out_dir, file), path],
                               check=True, capture_output=True)
                extracted[file] = True
        rows.append((event, 'voice', file, 1.0, ''))
    with open(os.path.join(out_dir, 'events.tsv'), 'w') as f:
        f.write('# index\tplay|loop|stop\tfile\tvolume\tstops  (tools/ttre/sfx.py: Bnk_0 events = the global sound events)\n')
        for k, what, file, vol, stops in rows:
            f.write(f'{k}\t{what}\t{file}\t{vol:.3f}\t{stops}\n')
    print(f'{out_dir}: {len(extracted)} sounds, {sum(1 for r in rows if r[2])}/{len(rows)} events with a sound')


if __name__ == '__main__':
    main(*sys.argv[1:])
