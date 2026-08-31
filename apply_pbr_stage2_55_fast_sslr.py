#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.55 HUD SSLR traversal budgets."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

SSLR_RE = re.compile(
    r"^(#define IE_PBR_STAGE243_HUD_SSLR_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
DEFER_RE = re.compile(
    r"^(#define IE_PBR_STAGE253_DEFER_HIT_RADIANCE )[0-1](\s*)$",
    re.MULTILINE,
)
QUALITY_RE = re.compile(
    r"^(#define IE_PBR_STAGE255_SSLR_QUALITY )[1-4](\s*)$",
    re.MULTILINE,
)

# sslr mode, deferred-radiance mode, traversal-quality mode
MODES = {
    "off": (0, 1, 1),
    "performance": (2, 1, 1),
    "production": (2, 1, 1),
    "balanced": (2, 1, 2),
    "quality": (2, 1, 3),
    "reference": (2, 0, 4),
}

QUALITY_NAMES = {
    1: "performance: 8 march + 2 binary steps, 2.00 m ray",
    2: "balanced: 12 march + 3 binary steps, 2.40 m ray",
    3: "quality: 16 march + 3 binary steps, 3.20 m ray",
    4: "Stage 2.45 reference: 32 march + 5 binary steps, 6.40 m ray",
}


def replace_once(pattern: re.Pattern[str], value: int, text: str, label: str) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_value(pattern: re.Pattern[str], text: str, label: str) -> int:
    match = pattern.search(text)
    if match is None:
        raise SystemExit(f"{label} define not found in {COMBINE}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not COMBINE.is_file():
        raise SystemExit("Stage 2.55 shader file was not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[off|performance|balanced|quality|reference|production|status]"
        )

    command = sys.argv[1].lower()
    text = COMBINE.read_text(encoding="utf-8")

    if command == "status":
        sslr = read_value(SSLR_RE, text, "HUD SSLR")
        quality = read_value(QUALITY_RE, text, "Stage 2.55 quality")
        if sslr == 0:
            print("Stage 2.55 HUD SSLR: disabled")
        else:
            print(f"Stage 2.55 HUD SSLR: {QUALITY_NAMES[quality]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    sslr, deferred, quality = MODES[command]
    text = replace_once(SSLR_RE, sslr, text, "HUD SSLR mode")
    text = replace_once(DEFER_RE, deferred, text, "Stage 2.53 deferred sampling")
    text = replace_once(QUALITY_RE, quality, text, "Stage 2.55 quality")
    COMBINE.write_text(text, encoding="utf-8", newline="\n")

    if sslr == 0:
        print("Stage 2.55 HUD SSLR: disabled")
    else:
        print(f"Stage 2.55 HUD SSLR: {QUALITY_NAMES[quality]}")
    print("Sky IBL, BRDF, local lighting, shadows, and metallic 3.50x are unchanged.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
