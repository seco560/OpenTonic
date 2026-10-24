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

Output: files/out/audio/sfx/<name>.wav (vgmstream-cli; bnm stream names carry the resource index) and events.tsv:
  index <tab> play|loop|stop <tab> file (relative to sfx/, "" = silent) <tab> volume 0..1 <tab> stop: the event it stops
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
    with open(os.path.join(out_dir, 'events.tsv'), 'w') as f:
        f.write('# index\tplay|loop|stop\tfile\tvolume\tstops  (tools/ttre/sfx.py: Bnk_0 events = the global sound events)\n')
        for k, what, file, vol, stops in rows:
            f.write(f'{k}\t{what}\t{file}\t{vol:.3f}\t{stops}\n')
    print(f'{out_dir}: {len(extracted)} sounds, {sum(1 for r in rows if r[2])}/{len(rows)} events with a sound')


if __name__ == '__main__':
    main(*sys.argv[1:])
