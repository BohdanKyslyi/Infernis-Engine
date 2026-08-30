#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.38 conductor transport modes."""

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
    r"^(#define IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE )[0-3](\s*)$",
    re.MULTILINE,
)

MODES = {
    "baseline": 0,
    "production": 0,
    "off": 0,
    "f0_bridge": 1,
    "irradiance_fill": 2,
    "combined": 3,
}
MODE_NAMES = {
    0: "linear-F0 reflection baseline",
    1: "bounded conductor F0 presentation bridge",
    2: "rough-conductor local and ambient irradiance fill",
    3: "F0 bridge plus rough-conductor irradiance fill",
}


def read_mode(text: str) -> int:
    match = DEFINE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.38 define not found in {SHADER}")
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
                "f0_bridge",
                "irradiance_fill",
                "combined",
                "status",
            )
        )
        raise SystemExit(f"Usage: py {Path(__file__).name} [{choices}]")

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.38 conductor transport: {MODE_NAMES[current]}")
        return

    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target = MODES[command]
    updated, count = DEFINE_RE.subn(rf"\g<1>{target}\g<2>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update Stage 2.38 conductor transport")

    SHADER.write_text(updated, encoding="utf-8", newline="\n")
    print(f"Stage 2.38 conductor transport: {MODE_NAMES[target]}")
    if target == 0:
        print("Production linear-F0 reflection response restored.")
    elif target == 1:
        print("Only the bounded conductor F0 bridge is enabled.")
    elif target == 2:
        print("Only the F0-tinted local and ambient irradiance fill is enabled.")
    else:
        print("Both conductor transport corrections are enabled.")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
