#!/usr/bin/env python3
"""Toggle Stage 2.23 PBR local-light routing diagnostics."""

from pathlib import Path
import re
import sys

MODES = {"off": 0, "route": 1, "status": None}

shader = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "lmodel.h"
)

if len(sys.argv) != 2 or sys.argv[1].lower() not in MODES:
    print("Usage: py apply_pbr_stage2_23_local_route_debug.py [off|route|status]")
    raise SystemExit(2)

if not shader.is_file():
    print(f"ERROR: shader not found: {shader}")
    raise SystemExit(1)

text = shader.read_text(encoding="utf-8")
pattern = r"(?m)^#define IE_PBR_LOCAL_ROUTE_MODE ([01])$"
match = re.search(pattern, text)

if not match:
    print("ERROR: IE_PBR_LOCAL_ROUTE_MODE marker not found in lmodel.h")
    raise SystemExit(1)

requested = MODES[sys.argv[1].lower()]
names = {0: "off", 1: "route"}

if requested is None:
    print(f"Stage 2.23 local route mode: {names[int(match.group(1))]}")
    raise SystemExit(0)

updated = re.sub(
    pattern,
    f"#define IE_PBR_LOCAL_ROUTE_MODE {requested}",
    text,
    count=1,
)
shader.write_text(updated, encoding="utf-8", newline="\n")

print(f"Stage 2.23 local route mode: {names[requested]}")
print("Delete shaders_cache before launching the game.")
