#!/usr/bin/env python3
"""Infernis PBR Stage 2.8 diagnostic: visualize G-buffer M/R/H channels.

Usage from the Infernis Engine repository root:

    py apply_pbr_stage2_8_debug.py on
    py apply_pbr_stage2_8_debug.py off

Diagnostic colors on PBR pixels:
    Red   = Metallic
    Green = Roughness
    Blue  = Hemi multiplied by texture AO

The debug output bypasses lighting, fog, and tonemapping for PBR pixels.
"""

from pathlib import Path
import sys


TARGET = Path("gamedata/shaders/r3/combine_1.ps")

NORMAL_BLOCK = """ _out o;
 tonemap (o.low, o.high, color, tm_scale ) ;
 o.low.a = skyblend ;
 o.high.a = skyblend ;
"""

DEBUG_BLOCK = """ _out o;
 tonemap (o.low, o.high, color, tm_scale ) ;

 // Infernis PBR Stage 2.8 diagnostic.
 // R = Metallic, G = Roughness, B = Hemi * texture AO.
 // This return bypasses every lighting pass and tonemapping contribution,
 // proving whether local lights can alter the stored material channels.
 if (gbd.pbr > 0.5f)
 {
     o.low = float4(
         saturate(gbd.metallic),
         saturate(gbd.roughness),
         saturate(gbd.hemi),
         1.0f
     );
     o.high = float4(0.0f, 0.0f, 0.0f, 0.0f);
     return o;
 }

 o.low.a = skyblend ;
 o.high.a = skyblend ;
"""


def main() -> int:
    mode = sys.argv[1].lower() if len(sys.argv) > 1 else "on"
    if mode not in {"on", "off"}:
        print("Usage: py apply_pbr_stage2_8_debug.py [on|off]")
        return 1

    target = Path.cwd() / TARGET
    if not target.is_file():
        print(f"ERROR: {TARGET} was not found.")
        print("Run this script from the root of the Infernis Engine repository.")
        return 1

    # Decode bytes directly so CRLF/LF style is preserved exactly.
    source = target.read_bytes().decode("utf-8")
    newline = "\r\n" if "\r\n" in source else "\n"
    normal_block = NORMAL_BLOCK.replace("\n", newline)
    debug_block = DEBUG_BLOCK.replace("\n", newline)

    if mode == "on":
        if debug_block in source:
            print("Stage 2.8 diagnostic is already enabled; nothing to do.")
            return 0
        if normal_block not in source:
            print("ERROR: expected combine_1.ps output block was not found.")
            print("No file was changed.")
            return 1
        updated = source.replace(normal_block, debug_block, 1)
        message = "Stage 2.8 diagnostic enabled. Delete shaders_cache and launch the game."
    else:
        if debug_block not in source:
            if normal_block in source:
                print("Stage 2.8 diagnostic is already disabled; nothing to do.")
                return 0
            print("ERROR: Stage 2.8 diagnostic block was not found.")
            print("No file was changed.")
            return 1
        updated = source.replace(debug_block, normal_block, 1)
        message = "Stage 2.8 diagnostic disabled; normal lighting restored."

    target.write_bytes(updated.encode("utf-8"))
    print(message)
    print(f"Changed: {TARGET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
