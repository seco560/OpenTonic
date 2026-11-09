"""The original demo tapes: copies Demos/<level>/DemoNNN.sav from the retail install to files/out/demos/<Level>/.

Run as `python3 -m tools.ttre.demos files/sources files/out`. The tapes sit beside GAMEDATA in the install (they are not
inside it): files/sources/Demos. Named as the level files are ("lecanyon" -> LeCanyon). A byte-for-byte copy of a tape
already taken for that level is left out (the install's three TotalSki tapes are one). docs/RECORDING.md "The original demos".
"""
import shutil
import sys
from pathlib import Path


def main(sources, out):
    src = Path(sources) / "Demos"
    dst = Path(out) / "demos"
    levels = {p.stem.lower(): p.stem for p in (Path(out) / "ttlv").glob("*.ttlv")}
    if dst.exists():
        shutil.rmtree(dst)
    if not src.is_dir():
        print("== original demos: no files/sources/Demos (copy the install's Demos folder there for Replays' demo button)")
        return
    taken = 0
    for folder in sorted(p for p in src.iterdir() if p.is_dir()):
        level = levels.get(folder.name.lower())
        if level is None:
            print(f"== original demos: no level for {folder.name}, skipped")
            continue
        seen = []
        for tape in sorted(folder.glob("*.[sS][aA][vV]")):
            data = tape.read_bytes()
            if data in seen:
                continue
            seen.append(data)
            (dst / level).mkdir(parents=True, exist_ok=True)
            (dst / level / tape.name).write_bytes(data)
            taken += 1
    print(f"== original demos: {taken} tapes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
