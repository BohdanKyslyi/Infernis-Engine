#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.33 rough-metal IBL audit modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path

SHADER = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "infernis_pbr_lighting.h"
)
DEFINE_RE = re.compile(
    r"^(#define IE_PBR_STAGE33_IBL_DEBUG_MODE )[0-2](\s*)$",
    re.MULTILINE,
)

MODES = {
    "off": 0,
    "production": 0,
    "raw_env": 1,
    "neutral_f0": 2,
}
MODE_NAMES = {
    0: "production",
    1: "raw environment radiance",
    2: "neutral metal F0 (0.65)",
}


def read_mode(text: str) -> int:
    match = DEFINE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.33 define not found in {SHADER}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not SHADER.is_file():
        raise SystemExit(f"Shader not found: {SHADER}")

    text = SHADER.read_text(encoding="utf-8")
    current = read_mode(text)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[off|raw_env|neutral_f0|status]"
        )

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.33 IBL audit: {MODE_NAMES[current]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target = MODES[command]
    updated, count = DEFINE_RE.subn(
        rf"\g<1>{target}\g<2>",
        text,
        count=1,
    )
    if count != 1:
        raise SystemExit("Could not update Stage 2.33 IBL audit")

    SHADER.write_text(updated, encoding="utf-8", newline="\n")
    print(f"Stage 2.33 IBL audit: {MODE_NAMES[target]}")
    print("Keep Stage 2.30 in metal_rough and delete shaders_cache.")


if __name__ == "__main__":
    main()
