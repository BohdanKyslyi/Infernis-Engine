#!/usr/bin/env python3
"""Toggle Stage 2.18 PBR sun accumulator routing diagnostics."""

from pathlib import Path
import re
import sys

MODES = {
    "off": 0,
    "zero": 1,
    "route": 2,
    "status": None,
}

shader = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "lmodel.h"
)

if len(sys.argv) != 2 or sys.argv[1].lower() not in MODES:
    print("Usage: py apply_pbr_stage2_18_sun_route_debug.py "
          "[off|zero|route|status]")
    raise SystemExit(2)

if not shader.is_file():
    print(f"ERROR: shader not found: {shader}")
    raise SystemExit(1)

text = shader.read_text(encoding="utf-8")
pattern = r"(?m)^#define IE_PBR_SUN_ROUTE_MODE ([012])$"
match = re.search(pattern, text)

if not match:
    print("ERROR: IE_PBR_SUN_ROUTE_MODE marker not found in lmodel.h")
    raise SystemExit(1)

names = {0: "off", 1: "zero", 2: "route"}
requested = MODES[sys.argv[1].lower()]

if requested is None:
    print(f"Stage 2.18 sun route mode: {names[int(match.group(1))]}")
    raise SystemExit(0)

updated, count = re.subn(
    pattern,
    f"#define IE_PBR_SUN_ROUTE_MODE {requested}",
    text,
    count=1,
)

if count != 1:
    print("ERROR: failed to update IE_PBR_SUN_ROUTE_MODE")
    raise SystemExit(1)

shader.write_text(updated, encoding="utf-8", newline="\n")
print(f"Stage 2.18 sun route mode: {names[requested]}")
print("Delete shaders_cache before launching the game.")
