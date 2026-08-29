#!/usr/bin/env python3
"""Toggle Stage 2.24 PBR local-light lobe diagnostics."""

from pathlib import Path
import re
import sys

MODES = {
    "full": 0,
    "diffuse": 1,
    "specular": 2,
    "cosine": 3,
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
    print(
        "Usage: py apply_pbr_stage2_24_local_lobe_debug.py "
        "[full|diffuse|specular|cosine|status]"
    )
    raise SystemExit(2)

if not shader.is_file():
    print(f"ERROR: shader not found: {shader}")
    raise SystemExit(1)

text = shader.read_text(encoding="utf-8")
pattern = r"(?m)^#define IE_PBR_LOCAL_LOBE_MODE ([0-3])$"
match = re.search(pattern, text)

if not match:
    print("ERROR: IE_PBR_LOCAL_LOBE_MODE marker not found in lmodel.h")
    raise SystemExit(1)

requested = MODES[sys.argv[1].lower()]
names = {
    0: "full",
    1: "diffuse",
    2: "specular",
    3: "cosine",
}

if requested is None:
    print(f"Stage 2.24 local lobe mode: {names[int(match.group(1))]}")
    raise SystemExit(0)

updated = re.sub(
    pattern,
    f"#define IE_PBR_LOCAL_LOBE_MODE {requested}",
    text,
    count=1,
)
shader.write_text(updated, encoding="utf-8", newline="\n")

print(f"Stage 2.24 local lobe mode: {names[requested]}")
print("Delete shaders_cache before launching the game.")
