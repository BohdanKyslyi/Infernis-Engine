#!/usr/bin/env python3
"""Switch the final Infernis PBR rough-metal strength candidates."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
LIGHTING = ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
COMBINE = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"

ENERGY_RE = re.compile(
    r"^(#define IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE )[0-6](\s*)$",
    re.MULTILINE,
)
OMNI_RE = re.compile(
    r"^(#define IE_PBR_STAGE248_HUD_OMNI_CONDUCTOR_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
SPLIT_RE = re.compile(
    r"^(#define IE_PBR_STAGE247_LOCAL_SPLIT_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
LOCAL_PROJECTION_RE = re.compile(
    r"^(#define IE_PBR_STAGE246_LOCAL_HUD_PROJECTION )[0-2](\s*)$",
    re.MULTILINE,
)
SSLR_RE = re.compile(
    r"^(#define IE_PBR_STAGE243_HUD_SSLR_MODE )[0-2](\s*)$",
    re.MULTILINE,
)
PROJECTION_RE = re.compile(
    r"^(#define IE_PBR_STAGE244_HUD_PROJECTION )[0-1](\s*)$",
    re.MULTILINE,
)
BINARY_RE = re.compile(
    r"^(#define IE_PBR_STAGE245_BINARY_HIT )[0-1](\s*)$",
    re.MULTILINE,
)
SKY_RE = re.compile(
    r"^(#define IE_PBR_STAGE241_SPECULAR_SKY_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": 0,
    "off": 0,
    "high": 4,
    "higher": 5,
    "maximum": 6,
}

NAMES = {
    0: "baseline single-scatter metallic GGX",
    4: "retained high conductor return (2.50x)",
    5: "higher conductor return (3.00x)",
    6: "maximum conductor return (3.50x)",
}


def replace_once(
    pattern: re.Pattern[str], value: int, text: str, label: str
) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def read_mode(text: str) -> int:
    match = ENERGY_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.52 define not found in {LIGHTING}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not LIGHTING.is_file() or not COMBINE.is_file():
        raise SystemExit("Stage 2.52 shader files were not found")

    choices = "baseline|high|higher|maximum|status"
    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    lighting = LIGHTING.read_text(encoding="utf-8")
    combine = COMBINE.read_text(encoding="utf-8")
    command = sys.argv[1].lower()

    if command == "status":
        mode = read_mode(lighting)
        print(f"Stage 2.52: {NAMES.get(mode, f'legacy mode {mode}')}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode = MODES[command]
    lighting = replace_once(ENERGY_RE, mode, lighting, "metal energy mode")
    lighting = replace_once(OMNI_RE, 0, lighting, "Stage 2.48 omni mode")
    lighting = replace_once(SPLIT_RE, 0, lighting, "Stage 2.47 split mode")
    lighting = replace_once(
        LOCAL_PROJECTION_RE, 0, lighting, "Stage 2.46 projection"
    )
    combine = replace_once(SSLR_RE, 2, combine, "Stage 2.43 SSLR")
    combine = replace_once(PROJECTION_RE, 1, combine, "Stage 2.44 projection")
    combine = replace_once(BINARY_RE, 1, combine, "Stage 2.45 binary hit")
    combine = replace_once(SKY_RE, 2, combine, "Stage 2.41 sky source")

    LIGHTING.write_text(lighting, encoding="utf-8", newline="\n")
    COMBINE.write_text(combine, encoding="utf-8", newline="\n")

    print(f"Stage 2.52: {NAMES[mode]}")
    print("Stages 2.46-2.49 diagnostics/corrections are disabled.")
    print("Stage 2.45 stable SSLR and sky-linear fallback remain enabled.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
