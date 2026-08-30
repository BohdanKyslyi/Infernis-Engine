#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.34 GGX multi-scattering compensation."""

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
    r"^(#define IE_PBR_STAGE34_MULTISCATTER )[0-1](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": 0,
    "off": 0,
    "compensated": 1,
    "on": 1,
}
MODE_NAMES = {
    0: "single-scattering baseline",
    1: "energy-compensated GGX multi-scattering",
}


def read_mode(text: str) -> int:
    match = DEFINE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.34 define not found in {SHADER}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not SHADER.is_file():
        raise SystemExit(f"Shader not found: {SHADER}")

    text = SHADER.read_text(encoding="utf-8")
    current = read_mode(text)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[baseline|compensated|status]"
        )

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.34: {MODE_NAMES[current]}")
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
        raise SystemExit("Could not update Stage 2.34 multi-scattering mode")

    SHADER.write_text(updated, encoding="utf-8", newline="\n")
    print(f"Stage 2.34: {MODE_NAMES[target]}")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
