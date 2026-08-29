#!/usr/bin/env python3
"""Infernis PBR Stage 2.8.1: safe G-buffer material diagnostic.

Run from the Infernis Engine repository root:

    py apply_pbr_stage2_8_1_debug.py on
    py apply_pbr_stage2_8_1_debug.py off

Diagnostic RGB on PBR pixels:
    R = Metallic
    G = Roughness
    B = Hemi multiplied by texture AO

The script recognizes and safely handles the normal shader, the broken
Stage 2.8 transparent debug block, and the corrected Stage 2.8.1 block.
"""

from pathlib import Path
import sys


TARGET = Path("gamedata/shaders/r3/combine_1.ps")

NORMAL_BLOCK = """ _out o;
 tonemap (o.low, o.high, color, tm_scale ) ;
 o.low.a = skyblend ;
 o.high.a = skyblend ;
"""

BROKEN_STAGE_2_8_BLOCK = """ _out o;
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

FIXED_STAGE_2_8_1_BLOCK = """ _out o;
 tonemap (o.low, o.high, color, tm_scale ) ;

 // Infernis PBR Stage 2.8.1 diagnostic.
 // R = Metallic, G = Roughness, B = Hemi * texture AO.
 // Preserve combine_1's inverse-alpha sky blending: alpha must remain
 // skyblend. Setting it to 1 would discard this RGB and reveal the background.
 if (gbd.pbr > 0.5f)
 {
     o.low = float4(
         saturate(gbd.metallic),
         saturate(gbd.roughness),
         saturate(gbd.hemi),
         skyblend
     );
     o.high = float4(0.0f, 0.0f, 0.0f, skyblend);
     return o;
 }

 o.low.a = skyblend ;
 o.high.a = skyblend ;
"""


def with_newline(block: str, newline: str) -> str:
    return block.replace("\n", newline)


def main() -> int:
    mode = sys.argv[1].lower() if len(sys.argv) > 1 else "on"
    if mode not in {"on", "off"}:
        print("Usage: py apply_pbr_stage2_8_1_debug.py [on|off]")
        return 1

    target = Path.cwd() / TARGET
    if not target.is_file():
        print(f"ERROR: {TARGET} was not found.")
        print("Run this script from the root of the Infernis Engine repository.")
        return 1

    source = target.read_bytes().decode("utf-8")
    newline = "\r\n" if "\r\n" in source else "\n"

    normal = with_newline(NORMAL_BLOCK, newline)
    broken = with_newline(BROKEN_STAGE_2_8_BLOCK, newline)
    fixed = with_newline(FIXED_STAGE_2_8_1_BLOCK, newline)

    if mode == "on":
        if fixed in source:
            print("Stage 2.8.1 diagnostic is already enabled; nothing to do.")
            return 0
        if broken in source:
            updated = source.replace(broken, fixed, 1)
            message = "Broken Stage 2.8 debug repaired and Stage 2.8.1 enabled."
        elif normal in source:
            updated = source.replace(normal, fixed, 1)
            message = "Stage 2.8.1 diagnostic enabled safely."
        else:
            print("ERROR: no recognized combine_1.ps output block was found.")
            print("No file was changed.")
            return 1
    else:
        if fixed in source:
            updated = source.replace(fixed, normal, 1)
            message = "Stage 2.8.1 diagnostic disabled; normal lighting restored."
        elif broken in source:
            updated = source.replace(broken, normal, 1)
            message = "Broken Stage 2.8 debug removed; normal lighting restored."
        elif normal in source:
            print("Material diagnostic is already disabled; nothing to do.")
            return 0
        else:
            print("ERROR: no recognized diagnostic block was found.")
            print("No file was changed.")
            return 1

    target.write_bytes(updated.encode("utf-8"))
    print(message)
    print(f"Changed: {TARGET}")
    if mode == "on":
        print("Delete shaders_cache before launching the game.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
