#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.44 corrected HUD SSLR modes."""

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
PROJECTION_RE = re.compile(
    r"^(#define IE_PBR_STAGE244_HUD_PROJECTION )[0-1](\s*)$",
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
    "baseline": (0, 0, 0),
    "off": (0, 0, 0),
    "projection_mask": (1, 1, 2),
    "corrected_sslr": (2, 1, 2),
    "production": (2, 1, 2),
}

NAMES = {
    (0, 0): "baseline",
    (1, 1): "corrected HUD projection hit mask",
    (2, 1): "corrected scene-visible HUD SSLR",
}


def replace_once(pattern: re.Pattern[str], value: int, text: str, label: str) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_value(pattern: re.Pattern[str], text: str, label: str) -> int:
    match = pattern.search(text)
    if match is None:
        raise SystemExit(f"{label} define not found in {COMBINE}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not COMBINE.is_file() or not HEADER.is_file():
        raise SystemExit("Stage 2.44 shader files were not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[baseline|projection_mask|corrected_sslr|production|status]"
        )

    command = sys.argv[1].lower()
    combine = COMBINE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")

    if command == "status":
        mode = read_value(SSLR_RE, combine, "Stage 2.43")
        projection = read_value(PROJECTION_RE, combine, "Stage 2.44")
        print(f"Stage 2.44: {NAMES.get((mode, projection), 'custom state')}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    sslr_mode, projection, sky_mode = MODES[command]
    combine = replace_once(SSLR_RE, sslr_mode, combine, "Stage 2.43 mode")
    combine = replace_once(PROJECTION_RE, projection, combine, "Stage 2.44 projection")
    combine = replace_once(SKY_RE, sky_mode, combine, "Stage 2.41 sky mode")
    header = replace_once(STAGE242_RE, 0, header, "Stage 2.42 mode")

    COMBINE.write_text(combine, encoding="utf-8", newline="\n")
    HEADER.write_text(header, encoding="utf-8", newline="\n")

    print(f"Stage 2.44: {NAMES[(sslr_mode, projection)]}")
    print("Stage 2.42 was restored to baseline.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
