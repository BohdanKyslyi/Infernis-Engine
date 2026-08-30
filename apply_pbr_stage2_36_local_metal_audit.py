#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.36 local-metal audit modes."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
COMBINE_SHADER = ROOT / "gamedata" / "shaders" / "r3" / "combine_1.ps"
LIGHT_SHADER = ROOT / "gamedata" / "shaders" / "r3" / "lmodel.h"

GBUFFER_RE = re.compile(
    r"^(#define IE_PBR_GBUFFER_DEBUG_MODE )[0-4](\s*)$",
    re.MULTILINE,
)
LOCAL_LOBE_RE = re.compile(
    r"^(#define IE_PBR_LOCAL_LOBE_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

# mode: (G-buffer diagnostic, local-light lobe)
MODES = {
    "production": (0, 0),
    "off": (0, 0),
    "orm": (2, 0),
    "albedo": (3, 0),
    "normal": (4, 0),
    "local_cosine": (0, 3),
    "local_specular": (0, 2),
}

MODE_NAMES = {
    (0, 0): "production",
    (2, 0): "G-buffer ORM (R=metallic, G=roughness, B=hemi*AO)",
    (3, 0): "G-buffer Albedo",
    (4, 0): "G-buffer view-space normal",
    (0, 3): "local-light cosine/Albedo probe",
    (0, 2): "local-light specular only",
}


def read_define(pattern: re.Pattern[str], text: str, shader: Path) -> int:
    match = pattern.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.36 marker not found in {shader}")
    return int(match.group(0).split()[2])


def replace_define(
    pattern: re.Pattern[str],
    text: str,
    value: int,
    shader: Path,
) -> str:
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update Stage 2.36 marker in {shader}")
    return updated


def describe_mode(gbuffer_mode: int, local_lobe_mode: int) -> str:
    return MODE_NAMES.get(
        (gbuffer_mode, local_lobe_mode),
        f"custom state (G-buffer={gbuffer_mode}, local lobe={local_lobe_mode})",
    )


def main() -> None:
    for shader in (COMBINE_SHADER, LIGHT_SHADER):
        if not shader.is_file():
            raise SystemExit(f"Shader not found: {shader}")

    combine_text = COMBINE_SHADER.read_text(encoding="utf-8")
    light_text = LIGHT_SHADER.read_text(encoding="utf-8")

    current_gbuffer = read_define(GBUFFER_RE, combine_text, COMBINE_SHADER)
    current_local_lobe = read_define(LOCAL_LOBE_RE, light_text, LIGHT_SHADER)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        choices = "|".join(
            (
                "production",
                "orm",
                "albedo",
                "normal",
                "local_cosine",
                "local_specular",
                "status",
            )
        )
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    command = sys.argv[1].lower()
    if command == "status":
        print(
            "Stage 2.36 local-metal audit: "
            f"{describe_mode(current_gbuffer, current_local_lobe)}"
        )
        return

    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target_gbuffer, target_local_lobe = MODES[command]
    combine_updated = replace_define(
        GBUFFER_RE,
        combine_text,
        target_gbuffer,
        COMBINE_SHADER,
    )
    light_updated = replace_define(
        LOCAL_LOBE_RE,
        light_text,
        target_local_lobe,
        LIGHT_SHADER,
    )

    COMBINE_SHADER.write_text(combine_updated, encoding="utf-8", newline="\n")
    LIGHT_SHADER.write_text(light_updated, encoding="utf-8", newline="\n")

    print(
        "Stage 2.36 local-metal audit: "
        f"{describe_mode(target_gbuffer, target_local_lobe)}"
    )
    if command == "orm":
        print("Expected can lid: red=1.00, green about 0.40, blue about 0.92.")
    elif command == "albedo":
        print("Expected can lid: light warm metal, about sRGB (187, 181, 155).")
    elif command == "normal":
        print("Rotate the lid and check for continuous normal changes without inversion.")
    elif command == "local_cosine":
        print("This checks light direction and NdotL without GGX/Fresnel.")
    elif command == "local_specular":
        print("This keeps only the local-light GGX/Fresnel lobe.")
    else:
        print("Production G-buffer and full local-light BRDF restored.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
