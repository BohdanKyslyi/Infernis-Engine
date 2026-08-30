#!/usr/bin/env python3
"""Calibrate the Infernis PBR Stage 2.39 combined conductor reference gain."""

from __future__ import annotations

import re
import sys
from pathlib import Path


SHADER = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "infernis_pbr_lighting.h"
)
MODE_RE = re.compile(
    r"^(#define IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE )[0-3](\s*)$",
    re.MULTILINE,
)
LOCAL_GAIN_RE = re.compile(
    r"^(static const float IE_PBR_CONDUCTOR_LOCAL_REFERENCE_STRENGTH = )"
    r"[0-9]+(?:\.[0-9]+)?f;(\s*)$",
    re.MULTILINE,
)
IBL_GAIN_RE = re.compile(
    r"^(static const float IE_PBR_CONDUCTOR_IBL_REFERENCE_STRENGTH = )"
    r"[0-9]+(?:\.[0-9]+)?f;(\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": (0, "1.0"),
    "production": (0, "1.0"),
    "off": (0, "1.0"),
    "combined_100": (3, "1.0"),
    "combined_110": (3, "1.10"),
    "combined_115": (3, "1.15"),
    "combined": (3, "1.15"),
    "combined_120": (3, "1.20"),
}


def read_value(pattern: re.Pattern[str], text: str, label: str) -> str:
    match = pattern.search(text)
    if match is None:
        raise SystemExit(f"{label} not found in {SHADER}")
    return match.group(0).split("=")[1].strip().rstrip("f;")


def read_mode(text: str) -> str:
    match = MODE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.39 mode not found in {SHADER}")
    return match.group(0).split()[2]


def replace_once(
    pattern: re.Pattern[str], replacement: str, text: str, label: str
) -> str:
    updated, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise SystemExit(f"Could not update {label}")
    return updated


def main() -> None:
    if not SHADER.is_file():
        raise SystemExit(f"Shader not found: {SHADER}")

    text = SHADER.read_text(encoding="utf-8")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        choices = "|".join(
            (
                "baseline",
                "combined_100",
                "combined_110",
                "combined_115",
                "combined_120",
                "status",
            )
        )
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    command = sys.argv[1].lower()
    if command == "status":
        mode = read_mode(text)
        local_gain = read_value(LOCAL_GAIN_RE, text, "local reference gain")
        ibl_gain = read_value(IBL_GAIN_RE, text, "IBL reference gain")
        print(
            "Stage 2.40 conductor gain: "
            f"Stage 2.39 mode={mode}, local={local_gain}, IBL={ibl_gain}"
        )
        return

    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target_mode, gain_text = MODES[command]
    text = replace_once(
        MODE_RE,
        rf"\g<1>{target_mode}\g<2>",
        text,
        "Stage 2.39 mode",
    )
    text = replace_once(
        LOCAL_GAIN_RE,
        rf"\g<1>{gain_text}f;\g<2>",
        text,
        "local reference gain",
    )
    text = replace_once(
        IBL_GAIN_RE,
        rf"\g<1>{gain_text}f;\g<2>",
        text,
        "IBL reference gain",
    )
    SHADER.write_text(text, encoding="utf-8", newline="\n")

    if target_mode == 0:
        print("Stage 2.40: production conductor response restored")
    else:
        percent = round((float(gain_text) - 1.0) * 100.0)
        print(
            "Stage 2.40: combined conductor reference "
            f"({percent:+d}% local and IBL gain)"
        )
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
