#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.31 rough-metal IBL diagnostics."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HEADER = ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

BRIDGE_RE = re.compile(
    r"^(#define IE_PBR_XRAY_SPECULAR_BRIDGE )[01](\s*)$",
    re.MULTILINE,
)
LOD_RE = re.compile(
    r"^(static const float IE_PBR_IBL_MAX_LOD = )(?:8\.0f|4\.0f)(;\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": (0, "8.0f"),
    "domain": (1, "8.0f"),
    "lod4": (0, "4.0f"),
    "combined": (1, "4.0f"),
}
MODE_NAMES = {
    (0, "8.0f"): "baseline (linear-decode, max LOD 8)",
    (1, "8.0f"): "domain (X-Ray specular domain, max LOD 8)",
    (0, "4.0f"): "lod4 (linear-decode, max LOD 4)",
    (1, "4.0f"): "combined (X-Ray specular domain, max LOD 4)",
}


def current_settings(header: str, combine: str) -> tuple[int, str]:
    bridge = BRIDGE_RE.search(header)
    lod = LOD_RE.search(combine)
    if bridge is None or lod is None:
        raise SystemExit("Stage 2.31 controls were not found")
    return int(bridge.group(0).split()[2]), lod.group(2)


def main() -> None:
    if not HEADER.is_file() or not COMBINE.is_file():
        raise SystemExit("Stage 2.31 shader files were not found")

    header = HEADER.read_text(encoding="utf-8")
    combine = COMBINE.read_text(encoding="utf-8")
    current = current_settings(header, combine)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[baseline|domain|lod4|combined|status]"
        )

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.31 rough-metal IBL: {MODE_NAMES[current]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    bridge, lod = MODES[command]
    header, bridge_count = BRIDGE_RE.subn(
        rf"\g<1>{bridge}\g<2>",
        header,
        count=1,
    )
    combine, lod_count = LOD_RE.subn(
        rf"\g<1>{lod}\g<2>",
        combine,
        count=1,
    )
    if bridge_count != 1 or lod_count != 1:
        raise SystemExit("Could not update Stage 2.31 controls")

    HEADER.write_text(header, encoding="utf-8", newline="\n")
    COMBINE.write_text(combine, encoding="utf-8", newline="\n")
    print(f"Stage 2.31 rough-metal IBL: {MODE_NAMES[(bridge, lod)]}")
    print("Keep Stage 2.30 in metal_rough and delete shaders_cache.")


if __name__ == "__main__":
    main()
