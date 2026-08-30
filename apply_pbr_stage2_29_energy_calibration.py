#!/usr/bin/env python3
"""Apply Stage 2.29 PBR energy-calibration presets."""

from pathlib import Path
import re
import sys

PRESETS = {
    "reference": {
        "SUN_DIFFUSE": 1.00,
        "SUN_SPECULAR": 1.00,
        "LOCAL_DIFFUSE": 1.00,
        "LOCAL_SPECULAR": 1.00,
        "AMBIENT_DIFFUSE": 1.00,
        "AMBIENT_SPECULAR": 1.00,
    },
    "balanced": {
        "SUN_DIFFUSE": 0.90,
        "SUN_SPECULAR": 1.00,
        "LOCAL_DIFFUSE": 0.80,
        "LOCAL_SPECULAR": 1.00,
        "AMBIENT_DIFFUSE": 0.90,
        "AMBIENT_SPECULAR": 1.00,
    },
}

shader = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "infernis_pbr_lighting.h"
)

if len(sys.argv) != 2 or sys.argv[1].lower() not in {
    *PRESETS,
    "status",
}:
    print(
        "Usage: py apply_pbr_stage2_29_energy_calibration.py "
        "[reference|balanced|status]"
    )
    raise SystemExit(2)

if not shader.is_file():
    print(f"ERROR: shader not found: {shader}")
    raise SystemExit(1)

text = shader.read_text(encoding="utf-8")
pattern_template = (
    r"(?m)^static const float IE_PBR_CAL_"
    r"{name} = ([0-9]+(?:\.[0-9]+)?)f;$"
)

values = {}
for name in PRESETS["reference"]:
    match = re.search(pattern_template.format(name=name), text)
    if not match:
        print(f"ERROR: calibration marker not found: {name}")
        raise SystemExit(1)
    values[name] = float(match.group(1))

mode = sys.argv[1].lower()
if mode == "status":
    print("Stage 2.29 PBR energy calibration:")
    for name, value in values.items():
        print(f"  {name.lower():<18} {value:.2f}")
    raise SystemExit(0)

for name, value in PRESETS[mode].items():
    pattern = pattern_template.format(name=name)
    replacement = (
        f"static const float IE_PBR_CAL_{name} = "
        f"{value:.2f}f;"
    )
    text = re.sub(pattern, replacement, text, count=1)

shader.write_text(text, encoding="utf-8", newline="\n")
print(f"Stage 2.29 PBR energy preset: {mode}")
print("Delete shaders_cache before launching the game.")
