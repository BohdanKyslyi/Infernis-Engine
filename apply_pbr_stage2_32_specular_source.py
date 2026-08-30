#!/usr/bin/env python3
"""Switch Infernis PBR Stage 2.32 specular cubemap source."""

from __future__ import annotations

import re
import sys
from pathlib import Path

SHADER = (
    Path(__file__).resolve().parent
    / "gamedata"
    / "shaders"
    / "r3"
    / "combine_1.ps"
)
DEFINE_RE = re.compile(
    r"^(#define IE_PBR_SPECULAR_SOURCE_ENV )[01](\s*)$",
    re.MULTILINE,
)

MODES = {
    "env": 1,
    "environment": 1,
    "sky": 0,
}
MODE_NAMES = {
    1: "environment cubemap (corrected)",
    0: "visible sky cubemap (old path)",
}


def read_mode(text: str) -> int:
    match = DEFINE_RE.search(text)
    if match is None:
        raise SystemExit(f"Stage 2.32 define not found in {SHADER}")
    return int(match.group(0).split()[2])


def main() -> None:
    if not SHADER.is_file():
        raise SystemExit(f"Shader not found: {SHADER}")

    text = SHADER.read_text(encoding="utf-8")
    current = read_mode(text)

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} [env|sky|status]"
        )

    command = sys.argv[1].lower()
    if command == "status":
        print(f"Stage 2.32 specular source: {MODE_NAMES[current]}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    target = MODES[command]
    updated, count = DEFINE_RE.subn(
        rf"\g<1>{target}\g<2>",
        text,
        count=1,
    )
    if count != 1:
        raise SystemExit("Could not update Stage 2.32 specular source")

    SHADER.write_text(updated, encoding="utf-8", newline="\n")
    print(f"Stage 2.32 specular source: {MODE_NAMES[target]}")
    print("Delete shaders_cache before launching the game.")


if __name__ == "__main__":
    main()
