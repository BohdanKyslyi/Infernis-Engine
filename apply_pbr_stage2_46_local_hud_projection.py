#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.46 local HUD projection modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
LIGHTING = ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

LOCAL_RE = re.compile(
    r"^(#define IE_PBR_STAGE246_LOCAL_HUD_PROJECTION )[0-2](\s*)$",
    re.MULTILINE,
)
SSLR_RE = re.compile(
    r"^(#define IE_PBR_STAGE243_HUD_SSLR_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
PROJECTION_RE = re.compile(
    r"^(#define IE_PBR_STAGE244_HUD_PROJECTION )[0-1](\s*)$",
    re.MULTILINE,
)
BINARY_RE = re.compile(
    r"^(#define IE_PBR_STAGE245_BINARY_HIT )[0-1](\s*)$",
    re.MULTILINE,
)
SKY_RE = re.compile(
    r"^(#define IE_PBR_STAGE241_SPECULAR_SKY_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    # Keep the proven Stage 2.45 reflection in every A/B mode.  Only the local
    # accumulator projection changes, so the comparison remains isolated.
    "baseline": (0, 2, 1, 1, 2),
    "hud_mask": (1, 2, 1, 1, 2),
    "corrected": (2, 2, 1, 1, 2),
    "production": (2, 2, 1, 1, 2),
}

NAMES = {
    0: "Stage 2.45 reflection with legacy local-light coordinates",
    1: "near-PBR HUD selection mask",
    2: "HUD-corrected local GGX and shadow coordinates",
}


def replace_once(pattern: re.Pattern[str], value: int, text: str, label: str) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_mode(text: str) -> int:
    match = LOCAL_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.46 define not found in {LIGHTING}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not LIGHTING.is_file() or not COMBINE.is_file():
        raise SystemExit("Stage 2.46 shader files were not found")

    choices = "baseline|hud_mask|corrected|production|status"
    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    lighting = LIGHTING.read_text(encoding="utf-8")
    combine = COMBINE.read_text(encoding="utf-8")
    command = sys.argv[1].lower()

    if command == "status":
        print(f"Stage 2.46: {NAMES[read_mode(lighting)]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    local_mode, sslr, projection, binary, sky = MODES[command]
    lighting = replace_once(LOCAL_RE, local_mode, lighting, "local HUD projection")
    combine = replace_once(SSLR_RE, sslr, combine, "Stage 2.43 SSLR")
    combine = replace_once(PROJECTION_RE, projection, combine, "Stage 2.44 projection")
    combine = replace_once(BINARY_RE, binary, combine, "Stage 2.45 binary hit")
    combine = replace_once(SKY_RE, sky, combine, "Stage 2.41 sky source")

    LIGHTING.write_text(lighting, encoding="utf-8", newline="\n")
    COMBINE.write_text(combine, encoding="utf-8", newline="\n")

    print(f"Stage 2.46: {NAMES[local_mode]}")
    print("Stage 2.45 stable SSLR and sky-linear fallback remain enabled.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
