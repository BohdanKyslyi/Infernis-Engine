#!/usr/bin/env python3
"""Isolate the complete HUD SSLR cost without changing the rest of PBR."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

SSLR_RE = re.compile(
    r"^(#define IE_PBR_STAGE243_HUD_SSLR_MODE )[0-2](\s*)$",
    re.MULTILINE,
)

MODES = {
    "sslr_off": 0,
    "off": 0,
    "sslr_on": 2,
    "on": 2,
    "production": 2,
}

NAMES = {
    0: "HUD SSLR disabled; sky IBL fallback retained",
    1: "HUD SSLR diagnostic mask",
    2: "HUD SSLR enabled with scene-visible reflections",
}


def read_mode(text: str) -> int:
    match = SSLR_RE.search(text)
    if match is None:
        raise SystemExit(f"HUD SSLR define not found in {COMBINE}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not COMBINE.is_file():
        raise SystemExit("Stage 2.54 shader file was not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[sslr_off|sslr_on|production|status]"
        )

    command = sys.argv[1].lower()
    text = COMBINE.read_text(encoding="utf-8")

    if command == "status":
        print(f"Stage 2.54 performance probe: {NAMES[read_mode(text)]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode = MODES[command]
    updated, count = SSLR_RE.subn(rf"\g<1>{mode}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update the HUD SSLR mode")

    COMBINE.write_text(updated, encoding="utf-8", newline="\n")

    print(f"Stage 2.54 performance probe: {NAMES[mode]}")
    print("Sky IBL, local lighting, shadows, BRDF, and metallic 3.50x are unchanged.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
