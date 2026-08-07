#!/usr/bin/env python3
"""Migrate hex-magical .solution files from version 3 to version 4.

Standalone `boost` polylines become crayon `stroke` lines with a full boost mask
(every segment boosted). Version bumps to 4; `boost` directives are removed.
"""

from __future__ import annotations

import pathlib
import sys


def mask_hex_all_boosted(seg_count: int) -> str:
    byte_count = (seg_count + 7) // 8
    return "ff" * byte_count


def migrate_text(text: str) -> str:
    out: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            out.append(raw)
            continue
        if line.startswith("version "):
            out.append("version 4")
            continue
        if line.startswith("boost"):
            rest = line[5:].strip()
            # Count "x,y" pairs
            pairs = [p for p in rest.split() if "," in p]
            if len(pairs) < 2:
                raise SystemExit(f"malformed boost line: {raw}")
            segs = len(pairs) - 1
            out.append(f"stroke {rest} mask {mask_hex_all_boosted(segs)}")
            continue
        out.append(raw)
    return "\n".join(out) + "\n"


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1] / "resources" / "solutions"
    paths = sorted(root.glob("*.solution")) if len(sys.argv) < 2 else [pathlib.Path(p) for p in sys.argv[1:]]
    for path in paths:
        text = path.read_text()
        if "version 4" in text.splitlines()[0:1] or text.startswith("version 4\n"):
            print(f"skip {path.name} (already v4)")
            continue
        path.write_text(migrate_text(text))
        print(f"migrated {path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
