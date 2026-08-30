#!/usr/bin/env python3
"""Toggle Stage 2.28 X-Ray diffuse-domain bridge."""

from pathlib import Path
import re
import sys

MODES = {
    "linear": 0,
    "xray": 1,
    "status": None,
}

shader = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "infernis_pbr_lighting.h"
)

if len(sys.argv) != 2 or sys.argv[1].lower() not in MODES:
    print(
        "Usage: py apply_pbr_stage2_28_diffuse_domain.py "
        "[linear|xray|status]"
    )
    raise SystemExit(2)

if not shader.is_file():
    print(f"ERROR: shader not found: {shader}")
    raise SystemExit(1)

text = shader.read_text(encoding="utf-8")
pattern = r"(?m)^#define IE_PBR_XRAY_DIFFUSE_BRIDGE ([01])$"
match = re.search(pattern, text)

if not match:
    print(
        "ERROR: IE_PBR_XRAY_DIFFUSE_BRIDGE marker not found "
        "in infernis_pbr_lighting.h"
    )
    raise SystemExit(1)

requested = MODES[sys.argv[1].lower()]
names = {
    0: "linear",
    1: "xray",
}

if requested is None:
    print(f"Stage 2.28 diffuse domain: {names[int(match.group(1))]}")
    raise SystemExit(0)

updated = re.sub(
    pattern,
    f"#define IE_PBR_XRAY_DIFFUSE_BRIDGE {requested}",
    text,
    count=1,
)
shader.write_text(updated, encoding="utf-8", newline="\n")

print(f"Stage 2.28 diffuse domain: {names[requested]}")
print("Delete shaders_cache before launching the game.")
