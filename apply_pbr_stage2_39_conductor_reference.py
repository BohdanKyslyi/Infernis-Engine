#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.39 conductor reference modes."""

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
DEFINE_RE = re.compile(
    r"^(#define IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": 0,
    "production": 0,
    "off": 0,
    "local_reference": 1,
    "ibl_reference": 2,
    "combined_reference": 3,
}
MODE_NAMES = {
    0: "production conductor response",
    1: "strong F0-tinted local-light reference",
    2: "strong F0-tinted IBL reference",
    3: "combined local-light and IBL reference",
}


def read_mode(text: str) -> int:
    match = DEFINE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.39 define not found in {SHADER}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not SHADER.is_file():
        raise SystemExit(f"Shader not found: {SHADER}")

    text = SHADER.read_text(encoding="utf-8")
    current = read_mode(text)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        choices = "|".join(
            (
                "baseline",
                "local_reference",
                "ibl_reference",
                "combined_reference",
                "status",
            )
        )
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.39 conductor reference: {MODE_NAMES[current]}")
        return

    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target = MODES[command]
    updated, count = DEFINE_RE.subn(rf"\g<1>{target}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update Stage 2.39 conductor reference")

    SHADER.write_text(updated, encoding="utf-8", newline="\n")
    print(f"Stage 2.39 conductor reference: {MODE_NAMES[target]}")
    if target == 0:
        print("Production conductor response restored.")
    elif target == 1:
        print("Only the strong local-light reference is enabled.")
    elif target == 2:
        print("Only the strong IBL reference is enabled.")
    else:
        print("Both strong conductor reference sources are enabled.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
