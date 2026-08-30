#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.42 HUD-conductor audit modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
HEADER = ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

MODE_RE = re.compile(
    r"^(#define IE_PBR_STAGE242_HUD_CONDUCTOR_MODE )[0-3](\s*)$",
    re.MULTILINE,
)
SKY_RE = re.compile(
    r"^(#define IE_PBR_STAGE241_SPECULAR_SKY_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": (0, 0),
    "off": (0, 0),
    "production": (0, 0),
    "local_shadowed": (1, 0),
    "hud_occlusion": (2, 2),
    "combined": (3, 2),
}

NAMES = {
    0: "Stage 2.41 baseline; no HUD conductor correction",
    1: "shadow-aware integrated local conductor response",
    2: "near-camera conductor sky suppression with sky_linear world IBL",
    3: "shadow-aware local conductor plus near-camera sky suppression",
}

OLD_MODE_RES = (
    re.compile(r"^(#define IE_PBR_STAGE237_LOCAL_METAL_MODE )[0-3](\s*)$", re.MULTILINE),
    re.compile(r"^(#define IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE )[0-3](\s*)$", re.MULTILINE),
    re.compile(r"^(#define IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE )[0-3](\s*)$", re.MULTILINE),
)


def replace_once(pattern: re.Pattern[str], value: int, text: str, label: str) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_mode(text: str) -> int:
    match = MODE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.42 define not found in {HEADER}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not HEADER.is_file() or not COMBINE.is_file():
        raise SystemExit("Stage 2.42 shader files were not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        choices = "baseline|local_shadowed|hud_occlusion|combined|status"
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    header = HEADER.read_text(encoding="utf-8")
    combine = COMBINE.read_text(encoding="utf-8")
    command = sys.argv[1].lower()

    if command == "status":
        print(f"Stage 2.42: {NAMES[read_mode(header)]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode, sky_mode = MODES[command]
    header = replace_once(MODE_RE, mode, header, "Stage 2.42 mode")
    combine = replace_once(SKY_RE, sky_mode, combine, "Stage 2.41 sky mode")

    for pattern in OLD_MODE_RES:
        header = replace_once(pattern, 0, header, "old conductor audit")

    HEADER.write_text(header, encoding="utf-8", newline="\n")
    COMBINE.write_text(combine, encoding="utf-8", newline="\n")

    print(f"Stage 2.42: {NAMES[mode]}")
    print("Stages 2.37-2.40 were restored to baseline.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
