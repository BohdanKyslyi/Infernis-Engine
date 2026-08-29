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
static const float IE_PBR_MIN_ROUGHNESS = 0.045f;


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
    float3 L
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
    // ============================================================
// TEMP: pure Lambert-like albedo test
// No GGX, no Fresnel, no Metallic.
// ============================================================

    return
        albedo *
        NdotL;
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

    float specularValue =
        D * G;

    float3 specular =
        float3(
            specularValue,
            specularValue,
            specularValue
        );

    // Metals have no normal diffuse component.
    float3 diffuse =
        albedo *
        (1.0f - metallic);

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