#!/usr/bin/env python3
"""Select the Infernis PBR Stage 2.55 HUD SSLR preset."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / "gamedata" / "configs" / "infernis_engine" / "engine_external.ltx"
SSLR_RE = re.compile(r"^(\s*sslr_mode\s*=\s*)(\w+)(\s*(?:;.*)?)$", re.MULTILINE)

MODES = {
    "off": "off",
    "performance": "performance",
    "production": "performance",
    "balanced": "balanced",
    "quality": "quality",
    "reference": "reference",
}

DESCRIPTIONS = {
    "off": "disabled; sky IBL fallback retained",
    "performance": "8 march + 2 binary steps, 2.00 m ray",
    "balanced": "12 march + 3 binary steps, 2.40 m ray",
    "quality": "16 march + 3 binary steps, 3.20 m ray",
    "reference": "32 march + 5 binary steps, 6.40 m ray",
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
            "[off|performance|balanced|quality|reference|production|status]"
        )

    command = sys.argv[1].lower()
    text = CONFIG.read_text(encoding="utf-8")

    if command == "status":
        mode = read_mode(text)
        print(f"Stage 2.55 HUD SSLR ({mode}): {DESCRIPTIONS.get(mode, 'unknown preset')}")
        return
    if command not in MODES:
        raise SystemExit(f"Unknown mode: {command}")

    mode = MODES[command]
    updated, count = SSLR_RE.subn(rf"\g<1>{mode}\g<3>", text, count=1)
    if count != 1:
        raise SystemExit("Could not update sslr_mode")
    CONFIG.write_text(updated, encoding="utf-8", newline="\n")

    print(f"Stage 2.55 HUD SSLR ({mode}): {DESCRIPTIONS[mode]}")
    print("The setting is now stored in engine_external.ltx.")
    print("Delete shaders_cache and restart the game after changing it.")


if __name__ == "__main__":
    main()
