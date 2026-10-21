"""List every stream in the game's sound banks (WORLD/SOUND/*.bnm, English/*.bnm) via vgmstream-cli.

Usage: python3 -m tools.audio_inventory [min_seconds] > out/audio/inventory.tsv
Prints bank, subsong, seconds, channels, encoding and the stream's internal name, longest first;
streams of at least min_seconds (default 20) are the music candidates.
"""
import glob
import json
import os
import subprocess
import sys

SOUND = 'files/sources/GAMEDATA/WORLD/SOUND'


def info(bank, sub):
    r = subprocess.run(['vgmstream-cli', '-m', '-I', '-s', str(sub), bank], capture_output=True, text=True)
    line = next((l for l in r.stdout.splitlines() if l.startswith('{')), None)
    return json.loads(line) if line else None


def main(min_seconds=20.0):
    rows = []
    for bank in sorted(glob.glob(f'{SOUND}/*.bnm') + glob.glob(f'{SOUND}/English/*.bnm')):
        first = info(bank, 1)
        if not first:
            continue
        n = first.get('streamInfo', {}).get('total', 1)
        for s in range(1, n + 1):
            j = first if s == 1 else info(bank, s)
            if not j:
                continue
            secs = j.get('numberOfSamples', 0) / max(1, j.get('sampleRate', 1))
            if secs >= min_seconds:
                rows.append((secs, os.path.relpath(bank, SOUND), s, j.get('channels'), j.get('encoding'),
                             j.get('streamInfo', {}).get('name', '')))
    for secs, bank, s, ch, enc, name in sorted(rows, reverse=True):
        print(f'{bank}\t{s}\t{secs:.1f}\t{ch}\t{enc}\t{name}')


if __name__ == '__main__':
    main(float(sys.argv[1]) if len(sys.argv) > 1 else 20.0)
