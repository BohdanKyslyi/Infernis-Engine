#ifndef INFERNIS_PBR_LIGHTING_H
#define INFERNIS_PBR_LIGHTING_H

// ============================================================
// Infernis Engine - Base PBR lighting
//
// Metallic / Roughness workflow
// GGX NDF
// Smith geometry
// Fresnel-Schlick
//
// Adapted for the classic X-Ray deferred lighting pipeline.
// ============================================================

static const float IE_PBR_EPSILON = 0.00001f;

// Infernis PBR Stage 2.9:
// keep analytical highlights finite for the classic X-Ray HDR pipeline and
// use physically normalized Lambert/GGX lobes.
static const float IE_PBR_MIN_ROUGHNESS = 0.08f;
static const float IE_PBR_INV_PI = 0.31830988618f;

// Infernis PBR Stage 2.10:
// A near-zero roughness GGX lobe can exceed the useful range of the classic
// X-Ray FP16 accumulator by several orders of magnitude. Preserve the lobe
// shape, but reject only its extreme analytical peak before Fresnel tinting.
//
// Infernis PBR Stage 2.11:
// Directional sun and local lights use different radiance scales in X-Ray.
// Keep the proven local-light response, but calibrate the sun independently so
// a low-roughness metal cannot saturate the complete weapon into white.
static const float IE_PBR_LOCAL_SPECULAR_LIMIT = 2.0f;
static const float IE_PBR_SUN_SPECULAR_LIMIT = 0.25f;

// Infernis PBR Stage 2.12:
// Screen-space normal variance is converted into an additional squared
// roughness term. The conservative cap stabilizes sub-pixel bump highlights
// without turning genuinely smooth metal into a uniformly matte material.
static const float IE_PBR_SPECULAR_AA_STRENGTH = 0.35f;
static const float IE_PBR_SPECULAR_AA_MAX_VARIANCE = 0.10f;


// ------------------------------------------------------------
// Infernis PBR Stage 2.6: private gamma-to-linear transport.
//
// USE_GAMMA_22 is disabled in the classic shader pack. Enabling it globally
// would also alter every legacy material, so PBR owns its conversion here.
// This mirrors IX-Ray's PushGamma(x) convention without touching legacy.
// ------------------------------------------------------------

static const float IE_PBR_GAMMA = 2.2f;

float3 ie_pbr_to_linear(float3 value)
{
    return pow(abs(value), IE_PBR_GAMMA);
}

float ie_pbr_to_linear(float value)
{
    return pow(abs(value), IE_PBR_GAMMA);
}

float3 ie_pbr_prepare_albedo(float3 albedo)
{
    return ie_pbr_to_linear(albedo);
}

float3 ie_pbr_prepare_radiance(float3 radiance)
{
    return ie_pbr_to_linear(radiance);
}

float3 ie_pbr_prepare_light_factor(float3 factor)
{
    return ie_pbr_to_linear(saturate(factor));
}

float ie_pbr_prepare_light_factor(float factor)
{
    return ie_pbr_to_linear(saturate(factor));
}


// ------------------------------------------------------------
// Infernis PBR Stage 2.12: geometric specular anti-aliasing
//
// A normal map can change faster than one screen pixel can represent. GGX then
// sees many near-mirror microfacets and produces temporal sparkle. Fold the
// local normal variance into perceptual roughness while keeping the authored
// texture value as the lower bound.
// ------------------------------------------------------------

float ie_pbr_filter_roughness(
    float3 normal,
    float roughness
)
{
    normal = normalize(normal);
    roughness =
        clamp(
            roughness,
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );

    float3 normalDx = ddx(normal);
    float3 normalDy = ddy(normal);

    float normalVariance =
        dot(normalDx, normalDx) +
        dot(normalDy, normalDy);

    float kernelRoughness2 =
        min(
            normalVariance * IE_PBR_SPECULAR_AA_STRENGTH,
            IE_PBR_SPECULAR_AA_MAX_VARIANCE
        );

    return
        clamp(
            sqrt(
                roughness * roughness +
                kernelRoughness2
            ),
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );
}


// ------------------------------------------------------------
// GGX / Trowbridge-Reitz normal distribution
// ------------------------------------------------------------

float ie_pbr_distribution_ggx(
    float NdotH,
    float roughness
)
{
    roughness =
        clamp(
            roughness,
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );

    float alpha =
        roughness * roughness;

    float alpha2 =
        alpha * alpha;

    float divider =
        NdotH * NdotH *
        (alpha2 - 1.0f) +
        1.0f;

    return
        alpha2 *
        IE_PBR_INV_PI *
        rcp(
            max(
                divider * divider,
                IE_PBR_EPSILON
            )
        );
}


// ------------------------------------------------------------
// Smith / Schlick geometry term
// ------------------------------------------------------------

float ie_pbr_geometry_smith(
    float NdotL,
    float NdotV,
    float roughness
)
{
    float r =
        roughness + 1.0f;

    float k =
        r * r * 0.125f;

    float invK =
        1.0f - k;

    float ggxL =
        rcp(
            max(
                k + NdotL * invK,
                IE_PBR_EPSILON
            )
        );

    float ggxV =
        rcp(
            max(
                k + NdotV * invK,
                IE_PBR_EPSILON
            )
        );

    return
        0.25f *
        ggxL *
        ggxV;
}


// ------------------------------------------------------------
// Fresnel-Schlick
// ------------------------------------------------------------

float3 ie_pbr_fresnel_schlick(
    float3 F0,
    float HdotV
)
{
    float factor =
        pow(
            1.0f - saturate(HdotV),
            5.0f
        );

    return
        F0 +
        (1.0f - F0) * factor;
}


// ------------------------------------------------------------
// Direct PBR BRDF
//
// N = surface normal
// V = surface -> camera
// L = surface -> light
// ------------------------------------------------------------

float3 ie_pbr_direct_brdf(
    float3 albedo,
    float metallic,
    float roughness,
    float3 N,
    float3 V,
    float3 L,
    float directSpecularLimit
)
{
    metallic = saturate(metallic);

    roughness =
        clamp(
            roughness,
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );

    N = normalize(N);
    V = normalize(V);
    L = normalize(L);

    // Stage 2.12: use the same authored roughness, widened only where the
    // screen-space normal field contains unresolved sub-pixel variation.
    roughness =
        ie_pbr_filter_roughness(
            N,
            roughness
        );

    float NdotL =
        saturate(dot(N, L));

    float NdotV =
        saturate(dot(N, V));

    if (
        NdotL <= IE_PBR_EPSILON ||
        NdotV <= IE_PBR_EPSILON
        )
    {
        return float3(
            0.0f,
            0.0f,
            0.0f
        );
    }
    // Infernis PBR Stage 2.7:
    // the temporary Lambert diagnostic is complete. Continue into the
    // metallic/roughness GGX + Smith + Fresnel-Schlick path below.
    float3 H =
        normalize(L + V);

    float NdotH =
        saturate(dot(N, H));

    float HdotV =
        saturate(dot(H, V));

    float D =
        ie_pbr_distribution_ggx(
            NdotH,
            roughness
        );

    float G =
        ie_pbr_geometry_smith(
            NdotL,
            NdotV,
            roughness
        );

    // Standard dielectric F0.
    // Metals get their F0 from Albedo.
    float3 F0 =
        lerp(
            float3(
                0.04f,
                0.04f,
                0.04f
            ),
            albedo,
            metallic
        );

    float3 F =
        ie_pbr_fresnel_schlick(
            F0,
            HdotV
        );

    // Stage 2.10/2.11: prevent sub-pixel mirror peaks from saturating
    // the old HDR/tonemap path. The caller selects a directional-sun or local
    // light ceiling; GGX shape, Fresnel tint, and diffuse remain unchanged.
    float specularValue =
        min(
            D * G,
            max(
                directSpecularLimit,
                IE_PBR_EPSILON
            )
        );

    float3 specular =
        float3(
            specularValue,
            specularValue,
            specularValue
        );

    // Metals have no normal diffuse component.
    float3 diffuse =
        albedo *
        (1.0f - metallic) *
        IE_PBR_INV_PI;

    // Same basic structure used by IX-Ray:
    // Diffuse*(1-F) + Specular*F
    float3 BRDF =
        lerp(
            diffuse,
            specular,
            F
        );

    return
        BRDF * NdotL;
}

#endif