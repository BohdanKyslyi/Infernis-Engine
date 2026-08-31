#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.53 SSLR sampling modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

MODE_RE = re.compile(
    r"^(#define IE_PBR_STAGE253_DEFER_HIT_RADIANCE )[0-1](\s*)$",
    re.MULTILINE,
)

MODES = {
    "reference": 0,
    "baseline": 0,
    "optimized": 1,
    "production": 1,
}

NAMES = {
    0: "Stage 2.45 reference: radiance fetched during every march step",
    1: "optimized: radiance fetched only for the accepted SSLR hit",
}


def read_mode(text: str) -> int:
    match = MODE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.53 define not found in {COMBINE}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not COMBINE.is_file():
        raise SystemExit("Stage 2.53 shader file was not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[reference|optimized|production|status]"
        )

    command = sys.argv[1].lower()
    text = COMBINE.read_text(encoding="utf-8")

    if command == "status":
        print(f"Stage 2.53 SSLR sampling: {NAMES[read_mode(text)]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode = MODES[command]
    updated, count = MODE_RE.subn(rf"\g<1>{mode}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update Stage 2.53 sampling mode")

    COMBINE.write_text(updated, encoding="utf-8", newline="\n")

    print(f"Stage 2.53 SSLR sampling: {NAMES[mode]}")
    print("The 32-step march, binary refinement, and reflection result are unchanged.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
