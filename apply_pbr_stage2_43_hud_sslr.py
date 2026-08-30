#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.43 inline HUD SSLR modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"
HEADER = ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"

SSLR_RE = re.compile(
    r"^(#define IE_PBR_STAGE243_HUD_SSLR_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
SKY_RE = re.compile(
    r"^(#define IE_PBR_STAGE241_SPECULAR_SKY_MODE )[0-3](\s*)$",
    re.MULTILINE,
)
STAGE242_RE = re.compile(
    r"^(#define IE_PBR_STAGE242_HUD_CONDUCTOR_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": (0, 0),
    "off": (0, 0),
    "production": (0, 0),
    "hit_mask": (1, 2),
    "sslr": (2, 2),
}

NAMES = {
    0: "Stage 2.41 baseline",
    1: "HUD SSLR hit mask (green=scene hit, red=fallback)",
    2: "scene-visible HUD SSLR with sky_linear fallback",
}


def replace_once(pattern: re.Pattern[str], value: int, text: str, label: str) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_mode(text: str) -> int:
    match = SSLR_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.43 define not found in {COMBINE}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not COMBINE.is_file() or not HEADER.is_file():
        raise SystemExit("Stage 2.43 shader files were not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[baseline|hit_mask|sslr|status]"
        )

    command = sys.argv[1].lower()
    combine = COMBINE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")

    if command == "status":
        print(f"Stage 2.43: {NAMES[read_mode(combine)]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    sslr_mode, sky_mode = MODES[command]
    combine = replace_once(SSLR_RE, sslr_mode, combine, "Stage 2.43 mode")
    combine = replace_once(SKY_RE, sky_mode, combine, "Stage 2.41 sky mode")
    header = replace_once(STAGE242_RE, 0, header, "Stage 2.42 mode")

    COMBINE.write_text(combine, encoding="utf-8", newline="\n")
    HEADER.write_text(header, encoding="utf-8", newline="\n")

    print(f"Stage 2.43: {NAMES[sslr_mode]}")
    print("Stage 2.42 was restored to baseline.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
