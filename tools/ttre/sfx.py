"""Ed's sound effects: the fix bank's events, extracted for the viewer (docs/NOTES.md "Sound events").

Sound banks (WORLD/SOUND/Bnk_<n>.bnm, after decoding nothing: plain little-endian):
  header { u32 version (0), u32 events_offset, u32 n_events, u32 resources_offset, u32 n_resources, u32 data_offset,
           ... }
  event (0x20) { u32 id, u32 kind (1 play, 2 stop), u32 target (play: the resource index; stop: the index of the event
                 it stops), u32 pitch spread (16.16: SND_004048e0 plays it at a random pitch in [1 - s, 1 + s); 5 events
                 have one), u32, u32, u32 (1 nearly always; not read), u32 follow (SND_sound3d_00404c40, each frame:
                 1 = its voices are placed again from where their object is now, 0 = they stay as they started) }
                 (the exe's request switch knows kinds 1, 2, 3, 4, 6-11; TT's banks hold only 1 and 2)
  resource (0x5c) { u32 id, u32 type (1 audio; 8 / 10 tiny raw waveforms, unused here), u32, u32 size,
                    u32 data offset, u32 volume (0x7f full), ..., file name }
The engine's global sound events are **Bnk_0's events** (84; the fix bank): the snapshot stores them as pointers
0x9bdfe0c + 0x20 x index, listed by fix.snd as { u32 index, u32 address } pairs (fix.rts relocates them). An event
plays the resource with its id; event 49 (0x00390000) has none: it is the footstep switch over the 0x0039xxxx
resources by ground material (EsCarpet ... EsEarthS, Edpasgen; its resource r2e is a type-8 table); here it plays the
generic step. An event that a stop event targets is a loop (flight: 77 played, 76 stops it).

The levels' own sounds (2026-11-02): each level's .snd (encrypted) lists { u32 event id, u32 address } for every place in its
memory holding a sound reference (script T34 nodes, animation sound events); an id is bank << 16 | event index, as the
exe's _SND_fn_pGetBinEvent reads it, and a play event's third word names its resource the same way (bank << 16 |
resource index). The core reports them as LEVEL_SOUND + the event's rank (level_events); files <bank>_<name>.wav.
Output: files/out/audio/sfx/<name>.wav (vgmstream-cli; bnm stream names carry the resource index) and events.tsv:
  index <tab> play|loop|stop|voice <tab> file (relative to sfx/, "" = silent) <tab> volume 0..1 <tab> stop: the event it stops
  <tab> follow (1: placed again every frame from its emitter, 0: where it started; docs/ENGINE.md "3D sound")
  <tab> pitch spread (0: none)
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


def bank_paths():
    """{bank number: path} of every bank file but Bnk_0 and the intro's 200: WORLD/SOUND/Bnk_<n>.bnm and the language
    folder's (English/Bnk_83..88, the voices), any letter case."""
    import glob
    out = {}
    for path in glob.glob(f'{SOUND}/*.[bB][nN][mM]') + glob.glob(f'{SOUND}/*/*.[bB][nN][mM]'):
        stem = os.path.splitext(os.path.basename(path))[0]
        if stem[:4].lower() != 'bnk_' or not stem[4:].isdigit():
            continue
        bank = int(stem[4:])
        if bank not in (0, 200):
            out.setdefault(bank, path)
    return out


def level_events():
    """Every event of the banks other than Bnk_0, as {key: (global index, bank, k, kind, target)}, key = bank << 16 | k,
    the way the exe names an event (_SND_fn_pGetBinEvent(id): the loaded bank number id >> 16, its event id & 0xFFFF;
    the intro's 0xC80000 is bank 200's event 0). The levels' .snd and the scripts hold these keys. index = LEVEL_SOUND +
    its rank in (bank, k) order. target (the event's third word) is a key too: a play event's resource (bank << 16 |
    resource index), a stop event's event. (Until 2026-11-14 the keys were matched against the event records' first
    word, another numbering: cavedoc's popcorn, 0x310000 / 0x320000 = Bnk_49 / Bnk_50's clicks and pops, played
    Bnk_20's rv05, a laugh, or nothing.)"""
    if not hasattr(level_events, 'cache'):
        rows = []
        for bank, path in bank_paths().items():
            try:
                events, _ = read_bank(path)
            except (struct.error, OSError):
                continue
            rows += [(bank, k, kind, target) for k, (_, kind, target) in enumerate(events)]
        rows.sort()
        level_events.cache = {(bank << 16) | k: (LEVEL_SOUND + i, bank, k, kind, target)
                              for i, (bank, k, kind, target) in enumerate(rows)}
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


def event_extras(path):
    """[(follow, pitch spread)] per event of a bank (the event record's +0x1C and +0x0C / 65536; module docstring)."""
    d = open(path, 'rb').read()
    _, ev_off, n_ev = struct.unpack_from('<3I', d, 0)
    return [(struct.unpack_from('<I', d, ev_off + 0x20 * k + 0x1C)[0] and 1,
             struct.unpack_from('<i', d, ev_off + 0x20 * k + 0x0C)[0] / 65536) for k in range(n_ev)]


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
    extras = event_extras(bank)
    by_index = {v[0]: (rid, v) for rid, v in resources.items()}
    streams = stream_index(bank)
    os.makedirs(out_dir, exist_ok=True)
    rows, extracted = [], {}
    loops = {target for _, kind, target in events if kind == 2}
    step_extras = (1, 0.0)
    for k, (eid, kind, target) in enumerate(events):
        if kind == 2:
            rows.append((k, 'stop', '', 1.0, target, 0, 0.0))
            continue
        if eid == FOOTSTEPS:
            step_extras = extras[k]
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
        rows.append((k, 'loop' if k in loops else 'play', file, (res[2] if res else 127) / 127, '', *extras[k]))
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
        rows.append((STEP_SOUND + v, 'play', file, (res[2] if res else 127) / 127, '', *step_extras))  # the switch's
    # The levels' own sounds (their banks' events that some level's .snd names): event LEVEL_SOUND + rank.
    import glob
    named = set()
    for f in glob.glob(f'{LEVELS}/*/*.snd'):
        from . import sna
        b = sna.decrypt(open(f, 'rb').read())
        named |= set(struct.unpack_from(f'<{len(b) // 4}I', b)[0::2])
    table = level_events()
    paths = bank_paths()
    loaded = {}  # bank -> (resources by index, stream index)

    def resource(key):
        """A play event's resource: (bank, (index, type, volume, name)) or None."""
        bank, ri = key >> 16, key & 0xFFFF
        if bank not in paths:
            return None
        if bank not in loaded:
            _, res_by_id = read_bank(paths[bank])
            loaded[bank] = ({v[0]: v for v in res_by_id.values()}, stream_index(paths[bank]))
        res = loaded[bank][0].get(ri)
        return (bank, res) if res else None

    loops_all = {target for _, _, _, kind, target in table.values() if kind == 2}
    extras_of = {}
    level_rows = 0
    for key in sorted(k for k in named if k in table):
        index, bank, k, kind, target = table[key]
        if bank not in extras_of:
            extras_of[bank] = event_extras(paths[bank])
        if kind == 2:  # stop: the event it stops (a key)
            rows.append((index, 'stop', '', 1.0, table[target][0] if target in table else '', 0, 0.0))
            level_rows += 1
            continue
        got, file, vol = resource(target), '', 127
        if got:
            rbank, res = got
            vol = res[2]
            streams = loaded[rbank][1]
            if res[1] == 1 and res[0] in streams:
                file = f'{rbank}_' + os.path.splitext(res[3].lower())[0] + '.wav'
                if file not in extracted:
                    subprocess.run(['vgmstream-cli', '-s', str(streams[res[0]]), '-o', os.path.join(out_dir, file),
                                    paths[rbank]], check=True, capture_output=True)
                    extracted[file] = True
        rows.append((index, 'loop' if key in loops_all else 'play', file, vol / 127, '', *extras_of[bank][k]))
        level_rows += 1
    print(f'levels\' own sounds: {level_rows} events')
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
        rows.append((event, 'voice', file, 1.0, '', 0, 0.0))
    with open(os.path.join(out_dir, 'events.tsv'), 'w') as f:
        f.write('# index\tplay|loop|stop\tfile\tvolume\tstops\tfollow\tspread  (tools/ttre/sfx.py: Bnk_0 events = the global '
                'sound events)\n')
        for k, what, file, vol, stops, follow, spread in rows:
            f.write(f'{k}\t{what}\t{file}\t{vol:.3f}\t{stops}\t{follow}\t{spread:.4f}\n')
    print(f'{out_dir}: {len(extracted)} sounds, {sum(1 for r in rows if r[2])}/{len(rows)} events with a sound')


if __name__ == '__main__':
    main(*sys.argv[1:])
