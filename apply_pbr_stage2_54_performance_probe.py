#!/usr/bin/env python3
"""Toggle HUD SSLR through engine_external.ltx for the Stage 2.54 probe."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / "gamedata" / "configs" / "infernis_engine" / "engine_external.ltx"
SSLR_RE = re.compile(r"^(\s*sslr_mode\s*=\s*)(\w+)(\s*(?:;.*)?)$", re.MULTILINE)

MODES = {
    "sslr_off": "off",
    "off": "off",
    "sslr_on": "performance",
    "on": "performance",
    "production": "performance",
}


def read_mode(text: str) -> str:
    match = SSLR_RE.search(text)
    if match is None:
        raise SystemExit(f"sslr_mode was not found in {CONFIG}")
    return match.group(2).lower()


def main() -> None:
    if not CONFIG.is_file():
        raise SystemExit("engine_external.ltx was not found")

    if len(sys.argv) != 2 or sys.argv[1] in {"-h", "--help"}:
        raise SystemExit(
            f"Usage: py {Path(__file__).name} "
            "[sslr_off|sslr_on|production|status]"
        )

    command = sys.argv[1].lower()
    text = CONFIG.read_text(encoding="utf-8")

    if command == "status":
        print(f"Stage 2.54 performance probe: sslr_mode = {read_mode(text)}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode = MODES[command]
    updated, count = SSLR_RE.subn(rf"\g<1>{mode}\g<3>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update sslr_mode")
    CONFIG.write_text(updated, encoding="utf-8", newline="\n")

    print(f"Stage 2.54 performance probe: sslr_mode = {mode}")
    print("Delete shaders_cache and restart the game after changing it.")


if __name__ == "__main__":
    main()
