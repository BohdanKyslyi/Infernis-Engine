#!/usr/bin/env python3
"""Toggle Stage 2.19 directional sun-pass diagnostics."""

from pathlib import Path
import re
import sys

MODES = {"off": 0, "pass": 1, "status": None}

shader = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "lmodel.h"
)

if len(sys.argv) != 2 or sys.argv[1].lower() not in MODES:
    print("Usage: py apply_pbr_stage2_19_sun_pass_debug.py [off|pass|status]")
    raise SystemExit(2)

if not shader.is_file():
    print(f"ERROR: shader not found: {shader}")
    raise SystemExit(1)

text = shader.read_text(encoding="utf-8")
pass_pattern = r"(?m)^#define IE_PBR_SUN_PASS_MODE ([01])$"
route_pattern = r"(?m)^#define IE_PBR_SUN_ROUTE_MODE ([012])$"
pass_match = re.search(pass_pattern, text)
route_match = re.search(route_pattern, text)

if not pass_match or not route_match:
    print("ERROR: Stage 2.18/2.19 marker not found in lmodel.h")
    raise SystemExit(1)

requested = MODES[sys.argv[1].lower()]
names = {0: "off", 1: "pass"}

if requested is None:
    print(f"Stage 2.19 sun pass mode: {names[int(pass_match.group(1))]}")
    print(f"Stage 2.18 sun route mode: {int(route_match.group(1))}")
    raise SystemExit(0)

updated = re.sub(pass_pattern, f"#define IE_PBR_SUN_PASS_MODE {requested}", text, count=1)
updated = re.sub(route_pattern, "#define IE_PBR_SUN_ROUTE_MODE 0", updated, count=1)
shader.write_text(updated, encoding="utf-8", newline="\n")

print(f"Stage 2.19 sun pass mode: {names[requested]}")
print("Stage 2.18 sun route mode: off")
print("Delete shaders_cache before launching the game.")
