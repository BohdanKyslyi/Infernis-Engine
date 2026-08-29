#!/usr/bin/env python3
"""Infernis PBR Stage 2.10: direct GGX highlight stabilization.

Run from the Infernis Engine repository root after Stage 2.9:

    py apply_pbr_stage2_10.py

Changed file:
    gamedata/shaders/r3/infernis_pbr_lighting.h

The patch limits only extreme analytical direct-light GGX peaks. Ambient IBL,
material textures, the G-buffer, legacy X-Ray lighting, and C++ are unchanged.
"""

from pathlib import Path
import sys


TARGET = Path("gamedata/shaders/r3/infernis_pbr_lighting.h")

OLD_CONSTANTS = """// Infernis PBR Stage 2.9:
// keep analytical highlights finite for the classic X-Ray HDR pipeline and
// use physically normalized Lambert/GGX lobes.
static const float IE_PBR_MIN_ROUGHNESS = 0.08f;
static const float IE_PBR_INV_PI = 0.31830988618f;
"""

NEW_CONSTANTS = """// Infernis PBR Stage 2.9:
// keep analytical highlights finite for the classic X-Ray HDR pipeline and
// use physically normalized Lambert/GGX lobes.
static const float IE_PBR_MIN_ROUGHNESS = 0.08f;
static const float IE_PBR_INV_PI = 0.31830988618f;

// Infernis PBR Stage 2.10:
// A near-zero roughness GGX lobe can exceed the useful range of the classic
// X-Ray FP16 accumulator by several orders of magnitude. Preserve the lobe
// shape, but reject only its extreme analytical peak before Fresnel tinting.
static const float IE_PBR_DIRECT_SPECULAR_LIMIT = 2.0f;
"""

OLD_SPECULAR_VALUE = """    float specularValue =
        D * G;
"""

NEW_SPECULAR_VALUE = """    // Stage 2.10: prevent sub-pixel mirror peaks from saturating the
    // old HDR/tonemap path into an opaque white patch. Normal GGX values pass
    // through unchanged; only the out-of-range peak is clipped.
    float specularValue =
        min(
            D * G,
            IE_PBR_DIRECT_SPECULAR_LIMIT
        );
"""


def with_newline(block: str, newline: str) -> str:
    return block.replace("\n", newline)


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"{label}: expected exactly one match, found {count}")
    return source.replace(old, new, 1)


def main() -> int:
    target = Path.cwd() / TARGET

    if not target.is_file():
        print(f"ERROR: {TARGET} was not found.")
        print("Run this script from the Infernis Engine repository root.")
        return 1

    source = target.read_bytes().decode("utf-8")

    if "Infernis PBR Stage 2.10:" in source:
        if "IE_PBR_DIRECT_SPECULAR_LIMIT" in source:
            print("Infernis PBR Stage 2.10 is already applied; nothing to do.")
            return 0

        print("ERROR: a partial Stage 2.10 installation was detected.")
        print("No file was changed.")
        return 1

    if "Infernis PBR Stage 2.9:" not in source:
        print("ERROR: Infernis PBR Stage 2.9 was not found.")
        print("Apply Stage 2.9 first. No file was changed.")
        return 1

    newline = "\r\n" if "\r\n" in source else "\n"

    try:
        updated = replace_once(
            source,
            with_newline(OLD_CONSTANTS, newline),
            with_newline(NEW_CONSTANTS, newline),
            "Stage 2.9 constants"
        )
        updated = replace_once(
            updated,
            with_newline(OLD_SPECULAR_VALUE, newline),
            with_newline(NEW_SPECULAR_VALUE, newline),
            "direct GGX specular response"
        )
    except ValueError as error:
        print(f"ERROR: {error}")
        print("The shader is not in the expected Stage 2.9 state.")
        print("No file was changed.")
        return 1

    target.write_bytes(updated.encode("utf-8"))

    print("Infernis PBR Stage 2.10 applied successfully.")
    print(f"Changed: {TARGET}")
    print("Enabled: direct GGX peak stabilization")
    print("Unchanged: IBL, textures, G-buffer, legacy lighting, and C++")
    print("Delete shaders_cache before testing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
