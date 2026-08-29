#!/usr/bin/env python3
"""Infernis PBR Stage 2.9: normalized direct GGX plus ambient specular IBL.

Run from the Infernis Engine repository root after disabling Stage 2.8/2.8.1:

    py apply_pbr_stage2_8_1_debug.py off
    py apply_pbr_stage2_9.py

Changed files:
    gamedata/shaders/r3/infernis_pbr_lighting.h
    gamedata/shaders/r3/combine_1.ps

Legacy X-Ray lighting and C++ code are not modified.
"""

from pathlib import Path
import sys


HEADER = Path("gamedata/shaders/r3/infernis_pbr_lighting.h")
COMBINE = Path("gamedata/shaders/r3/combine_1.ps")

OLD_CONSTANTS = """static const float IE_PBR_EPSILON = 0.00001f;
static const float IE_PBR_MIN_ROUGHNESS = 0.045f;
"""

NEW_CONSTANTS = """static const float IE_PBR_EPSILON = 0.00001f;

// Infernis PBR Stage 2.9:
// keep analytical highlights finite for the classic X-Ray HDR pipeline and
// use physically normalized Lambert/GGX lobes.
static const float IE_PBR_MIN_ROUGHNESS = 0.08f;
static const float IE_PBR_INV_PI = 0.31830988618f;
"""

OLD_DISTRIBUTION_RETURN = """    return
        alpha2 *
        rcp(
            max(
                divider * divider,
                IE_PBR_EPSILON
            )
        );
"""

NEW_DISTRIBUTION_RETURN = """    return
        alpha2 *
        IE_PBR_INV_PI *
        rcp(
            max(
                divider * divider,
                IE_PBR_EPSILON
            )
        );
"""

OLD_DIFFUSE = """    float3 diffuse =
        albedo *
        (1.0f - metallic);
"""

NEW_DIFFUSE = """    float3 diffuse =
        albedo *
        (1.0f - metallic) *
        IE_PBR_INV_PI;
"""

IBL_ANCHOR = """uniform Texture2D s_half_depth;
"""

IBL_HELPERS = """uniform Texture2D s_half_depth;


// ============================================================
// Infernis PBR Stage 2.9 - ambient image-based lighting
//
// The combine pass already receives env_s0/env_s1 and sky_s0/sky_s1 from
// blender_combine.cpp. Diffuse irradiance uses the environment cubemaps;
// roughness-filtered specular irradiance uses the sky cubemaps.
// ============================================================

static const float IE_PBR_IBL_MAX_LOD = 8.0f;

float2 ie_pbr_environment_brdf(
    float NdotV,
    float roughness
)
{
    // IX-Ray's fitted split-sum environment BRDF approximation.
    NdotV = min(saturate(NdotV), 0.998f);
    roughness = saturate(roughness);

    float nsqr = NdotV * NdotV;
    float rsqr = roughness * roughness;

    float4 fac =
        float4(0.0187f, 1.0133f, 1.0000f, 1.0000f) +
        float4(1.9496f, -2.4717f, -0.0333f, 2.0508f) * NdotV +
        float4(1.2265f, -1.2172f, -1.3097f, 0.2342f) * roughness +
        float4(-7.6907f, 3.4300f, 0.5972f, -26.9406f) * NdotV * roughness +
        float4(18.3314f, 1.4794f, 19.3537f, 11.1429f) * nsqr +
        float4(-0.2894f, 0.5564f, 1.5052f, 7.0828f) * rsqr +
        float4(-19.3056f, -2.2456f, -28.2302f, 18.5470f) * nsqr * roughness +
        float4(7.0144f, -1.8934f, 1.3307f, 50.6469f) * NdotV * rsqr +
        float4(1.5728f, 1.3618f, 15.2939f, -63.3557f) * nsqr * rsqr;

    return saturate(fac.xy / fac.zw);
}

float3 ie_pbr_diffuse_irradiance(float3 normalView)
{
    float3 normalWorld =
        normalize(mul(m_v2w, normalize(normalView)));

    float3 env0 =
        env_s0.SampleLevel(smp_rtlinear, normalWorld, 0.0f).rgb;

    float3 env1 =
        env_s1.SampleLevel(smp_rtlinear, normalWorld, 0.0f).rgb;

    float3 irradiance =
        env_color.xyz *
        lerp(env0, env1, env_color.w);

    return ie_pbr_prepare_radiance(irradiance);
}

float3 ie_pbr_specular_irradiance(
    float3 pointView,
    float3 normalView,
    float roughness
)
{
    float3 viewRay = normalize(pointView);
    float3 reflectionView =
        reflect(viewRay, normalize(normalView));

    float3 reflectionWorld =
        normalize(mul(m_v2w, reflectionView));

    float lod =
        clamp(
            roughness,
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        ) * IE_PBR_IBL_MAX_LOD;

    float3 sky0 =
        sky_s0.SampleLevel(smp_rtlinear, reflectionWorld, lod).rgb;

    float3 sky1 =
        sky_s1.SampleLevel(smp_rtlinear, reflectionWorld, lod).rgb;

    float3 irradiance =
        env_color.xyz *
        lerp(sky0, sky1, env_color.w);

    return ie_pbr_prepare_radiance(irradiance);
}

float3 ie_pbr_ambient_ibl(
    float3 pointView,
    float3 normalView,
    float3 albedo,
    float metallic,
    float roughness,
    float hemiAo,
    float screenOcclusion
)
{
    metallic = saturate(metallic);
    roughness =
        clamp(
            roughness,
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );

    float3 N = normalize(normalView);
    float3 V = -normalize(pointView);
    float NdotV = saturate(dot(N, V));

    float3 diffuseIrradiance =
        ie_pbr_diffuse_irradiance(N) +
        ie_pbr_prepare_radiance(L_ambient.rgb);

    float3 specularIrradiance =
        ie_pbr_specular_irradiance(
            pointView,
            N,
            roughness
        );

    float3 diffuse =
        diffuseIrradiance *
        albedo *
        (1.0f - metallic);

    float3 F0 =
        lerp(
            float3(0.04f, 0.04f, 0.04f),
            albedo,
            metallic
        );

    float2 envBrdf =
        ie_pbr_environment_brdf(
            NdotV,
            roughness
        );

    float3 environmentFresnel =
        saturate(
            F0 * envBrdf.x +
            envBrdf.y
        );

    float3 ambient =
        lerp(
            diffuse,
            specularIrradiance,
            environmentFresnel
        );

    return
        ambient *
        saturate(hemiAo) *
        saturate(screenOcclusion);
}
"""

OLD_PBR_COMBINE = """if (gbd.pbr > 0.5f)
{
    // Direct lighting in accumulator is already
    // a complete RGB PBR BRDF.
    float3 direct =
        L.rgb;

    // Temporary simple indirect diffuse.
    // Proper IBL comes later.
    // Infernis PBR Stage 2.6: the temporary ambient term
    // uses the same linear-light convention as direct PBR lighting.
    float3 ambientDiffuse =
        ie_pbr_prepare_radiance(L_ambient.rgb) *
        ie_pbr_prepare_albedo(D.rgb) *
        (1.0f - gbd.metallic);

    // gbd.hemi already includes texture AO.
    ambientDiffuse *=
        saturate(gbd.hemi) *
        occ;

    color =
        direct +
        ambientDiffuse;
}
"""

NEW_PBR_COMBINE = """if (gbd.pbr > 0.5f)
{
    // Direct lighting in the accumulator is already a complete RGB BRDF.
    float3 direct = L.rgb;

    // Infernis PBR Stage 2.9: diffuse + roughness-filtered specular IBL.
    // Metallic surfaces receive their indirect color from reflected sky
    // radiance instead of the removed Lambert fallback.
    float3 ambient =
        ie_pbr_ambient_ibl(
            P.xyz,
            N.xyz,
            ie_pbr_prepare_albedo(D.rgb),
            gbd.metallic,
            gbd.roughness,
            gbd.hemi,
            occ
        );

    color = direct + ambient;
}
"""


def nl(block: str, newline: str) -> str:
    return block.replace("\n", newline)


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise ValueError(f"{label}: expected exactly one match, found {count}")
    return source.replace(old, new, 1)


def main() -> int:
    root = Path.cwd()
    header_path = root / HEADER
    combine_path = root / COMBINE

    for path in (header_path, combine_path):
        if not path.is_file():
            print(f"ERROR: {path.relative_to(root)} was not found.")
            print("Run this script from the Infernis Engine repository root.")
            return 1

    header = header_path.read_bytes().decode("utf-8")
    combine = combine_path.read_bytes().decode("utf-8")

    header_done = "Infernis PBR Stage 2.9:" in header
    combine_done = "Infernis PBR Stage 2.9 - ambient image-based lighting" in combine
    if header_done and combine_done:
        print("Infernis PBR Stage 2.9 is already applied; nothing to do.")
        return 0
    if header_done != combine_done:
        print("ERROR: a partial Stage 2.9 installation was detected.")
        print("No file was changed.")
        return 1

    if "Stage 2.8 diagnostic" in combine or "Stage 2.8.1 diagnostic" in combine:
        print("ERROR: a Stage 2.8 material diagnostic is still enabled.")
        print("Run: py apply_pbr_stage2_8_1_debug.py off")
        print("No file was changed.")
        return 1

    hnl = "\r\n" if "\r\n" in header else "\n"
    cnl = "\r\n" if "\r\n" in combine else "\n"

    try:
        updated_header = header
        updated_header = replace_once(
            updated_header,
            nl(OLD_CONSTANTS, hnl),
            nl(NEW_CONSTANTS, hnl),
            "PBR constants"
        )
        updated_header = replace_once(
            updated_header,
            nl(OLD_DISTRIBUTION_RETURN, hnl),
            nl(NEW_DISTRIBUTION_RETURN, hnl),
            "GGX distribution normalization"
        )
        updated_header = replace_once(
            updated_header,
            nl(OLD_DIFFUSE, hnl),
            nl(NEW_DIFFUSE, hnl),
            "direct diffuse normalization"
        )

        updated_combine = combine
        updated_combine = replace_once(
            updated_combine,
            nl(IBL_ANCHOR, cnl),
            nl(IBL_HELPERS, cnl),
            "IBL helper insertion"
        )
        updated_combine = replace_once(
            updated_combine,
            nl(OLD_PBR_COMBINE, cnl),
            nl(NEW_PBR_COMBINE, cnl),
            "PBR combine branch"
        )
    except ValueError as error:
        print(f"ERROR: {error}")
        print("The shaders are not in the expected Stage 2.7 production state.")
        print("No file was changed.")
        return 1

    # All preflight replacements succeeded; only now write both files.
    header_path.write_bytes(updated_header.encode("utf-8"))
    combine_path.write_bytes(updated_combine.encode("utf-8"))

    print("Infernis PBR Stage 2.9 applied successfully.")
    print(f"Changed: {HEADER}")
    print(f"Changed: {COMBINE}")
    print("Enabled: normalized direct GGX and cubemap ambient specular IBL")
    print("Unchanged: legacy lighting and C++")
    print("Delete shaders_cache before testing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
