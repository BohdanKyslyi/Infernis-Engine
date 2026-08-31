#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.47 local-light split diagnostics."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
LIGHTING = ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

SPLIT_RE = re.compile(
    r"^(#define IE_PBR_STAGE247_LOCAL_SPLIT_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
LOCAL_PROJECTION_RE = re.compile(
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
    "baseline": 0,
    "off": 0,
    "split_probe": 1,
    "shadowless": 2,
}

NAMES = {
    0: "Stage 2.45 reflection and ordinary local-light transport",
    1: "red raw-GGX / green visibility / blue omni-route probe",
    2: "selected-HUD local GGX without projected visibility",
}


def replace_once(
    pattern: re.Pattern[str], value: int, text: str, label: str
) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_mode(text: str) -> int:
    match = SPLIT_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.47 define not found in {LIGHTING}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not LIGHTING.is_file() or not COMBINE.is_file():
        raise SystemExit("Stage 2.47 shader files were not found")

    choices = "baseline|split_probe|shadowless|status"
    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    lighting = LIGHTING.read_text(encoding="utf-8")
    combine = COMBINE.read_text(encoding="utf-8")
    command = sys.argv[1].lower()

    if command == "status":
        print(f"Stage 2.47: {NAMES[read_mode(lighting)]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode = MODES[command]
    lighting = replace_once(SPLIT_RE, mode, lighting, "local split mode")
    lighting = replace_once(
        LOCAL_PROJECTION_RE, 0, lighting, "Stage 2.46 projection"
    )
    combine = replace_once(SSLR_RE, 2, combine, "Stage 2.43 SSLR")
    combine = replace_once(PROJECTION_RE, 1, combine, "Stage 2.44 projection")
    combine = replace_once(BINARY_RE, 1, combine, "Stage 2.45 binary hit")
    combine = replace_once(SKY_RE, 2, combine, "Stage 2.41 sky source")

    LIGHTING.write_text(lighting, encoding="utf-8", newline="\n")
    COMBINE.write_text(combine, encoding="utf-8", newline="\n")

    print(f"Stage 2.47: {NAMES[mode]}")
    print("Stage 2.46 local correction is disabled for an isolated comparison.")
    print("Stage 2.45 stable SSLR and sky-linear fallback remain enabled.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
