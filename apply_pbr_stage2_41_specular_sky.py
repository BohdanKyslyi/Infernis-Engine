#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.41 specular-sky audit modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
COMBINE_SHADER = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"
LIGHTING_HEADER = (
    ROOT / "gamedata" / "shaders" / "r3" / "infernis_pbr_lighting.h"
)

MODE_RE = re.compile(
    r"^(#define IE_PBR_STAGE241_SPECULAR_SKY_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": 0,
    "production": 0,
    "off": 0,
    "env_auto_lod": 1,
    "sky_linear": 2,
    "sky_native": 3,
}
MODE_NAMES = {
    0: "env_s source with fixed Stage 2.31 LOD range",
    1: "env_s source with runtime mip count",
    2: "full sky_s specular IBL with linear PBR transport",
    3: "full sky_s specular IBL in sampled color domain",
}

RESET_DEFINES = (
    re.compile(
        r"^(#define IE_PBR_MATERIAL_OVERRIDE_MODE )[0-4](\s*)$",
        re.MULTILINE,
    ),
    re.compile(
        r"^(#define IE_PBR_STAGE237_LOCAL_METAL_MODE )[0-3](\s*)$",
        re.MULTILINE,
    ),
    re.compile(
        r"^(#define IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE )[0-3](\s*)$",
        re.MULTILINE,
    ),
    re.compile(
        r"^(#define IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE )[0-3](\s*)$",
        re.MULTILINE,
    ),
)

RESET_GAINS = (
    re.compile(
        r"^(static const float IE_PBR_CONDUCTOR_LOCAL_REFERENCE_STRENGTH = )"
        r"[0-9]+(?:\.[0-9]+)?f;(\s*)$",
        re.MULTILINE,
    ),
    re.compile(
        r"^(static const float IE_PBR_CONDUCTOR_IBL_REFERENCE_STRENGTH = )"
        r"[0-9]+(?:\.[0-9]+)?f;(\s*)$",
        re.MULTILINE,
    ),
)


def read_mode(text: str) -> int:
    match = MODE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.41 define not found in {COMBINE_SHADER}")
    return int(match.group(0).split()[2])


def reset_old_audits(text: str) -> str:
    for pattern in RESET_DEFINES:
        text, count = pattern.subn(r"\g<1>0\g<2>", text, count=1)
        if count != 1:
            raise SystemExit(
                "Could not restore a prior PBR calibration define in "
                f"{LIGHTING_HEADER}"
            )
    for pattern in RESET_GAINS:
        text, count = pattern.subn(r"\g<1>1.0f;\g<2>", text, count=1)
        if count != 1:
            raise SystemExit(
                "Could not restore a Stage 2.40 conductor gain in "
                f"{LIGHTING_HEADER}"
            )
    return text


def main() -> None:
    if not COMBINE_SHADER.is_file():
        raise SystemExit(f"Shader not found: {COMBINE_SHADER}")
    if not LIGHTING_HEADER.is_file():
        raise SystemExit(f"Shader not found: {LIGHTING_HEADER}")

    combine_text = COMBINE_SHADER.read_text(encoding="utf-8")
    current = read_mode(combine_text)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        choices = "|".join(
            (
                "baseline",
                "env_auto_lod",
                "sky_linear",
                "sky_native",
                "status",
            )
        )
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.41 specular sky: {MODE_NAMES[current]}")
        return

    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target = MODES[command]
    combine_text, count = MODE_RE.subn(
        rf"\g<1>{target}\g<2>",
        combine_text,
        count=1,
    )
    if count != 1:
        raise SystemExit("Could not update Stage 2.41 specular-sky mode")

    lighting_text = LIGHTING_HEADER.read_text(encoding="utf-8")
    lighting_text = reset_old_audits(lighting_text)

    COMBINE_SHADER.write_text(combine_text, encoding="utf-8", newline="\n")
    LIGHTING_HEADER.write_text(lighting_text, encoding="utf-8", newline="\n")

    print(f"Stage 2.41 specular sky: {MODE_NAMES[target]}")
    print("Stage 2.30 and Stages 2.37-2.40 were restored to baseline.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
