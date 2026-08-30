#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.30 material-response calibration modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path

SHADER = Path(__file__).resolve().parent / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
DEFINE_RE = re.compile(r"^(#define IE_PBR_MATERIAL_OVERRIDE_MODE )[0-4](\s*)$", re.MULTILINE)

MODES = {
    "off": 0,
    "authored": 0,
    "dielectric_rough": 1,
    "dielectric_smooth": 2,
    "metal_rough": 3,
    "metal_smooth": 4,
}
MODE_NAMES = {
    0: "off (authored material)",
    1: "rough dielectric (metallic=0.00, roughness=0.85)",
    2: "smooth dielectric (metallic=0.00, roughness=0.20)",
    3: "rough metal (metallic=1.00, roughness=0.75)",
    4: "smooth metal (metallic=1.00, roughness=0.15)",
}


def read_mode(text: str) -> int:
    match = DEFINE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.30 define not found in {SHADER}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not SHADER.is_file():
        raise SystemExit(f"Shader not found: {SHADER}")

    text = SHADER.read_text(encoding="utf-8")
    current = read_mode(text)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        choices = "|".join(("off", "dielectric_rough", "dielectric_smooth", "metal_rough", "metal_smooth", "status"))
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.30 material override: {MODE_NAMES[current]}")
        return

    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target = MODES[command]
    updated, count = DEFINE_RE.subn(rf"\g<1>{target}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update Stage 2.30 material override")

    SHADER.write_text(updated, encoding="utf-8", newline="\n")
    print(f"Stage 2.30 material override: {MODE_NAMES[target]}")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
