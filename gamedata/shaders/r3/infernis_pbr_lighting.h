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

// Infernis PBR Stage 2.37: local-metal response audit.
// 0 = baseline, 1 = conductor multi-scattering, 2 = finite-source broadening,
// 3 = both corrections. Production remains at the proven baseline until the
// in-game A/B test selects the stable local-light response.
#define IE_PBR_STAGE237_LOCAL_METAL_MODE 0

// X-Ray local lights are mathematical points. Add a conservative angular
// variance only in the finite-source audit modes; combining variances keeps
// smooth authored metals smoother than rough ones instead of applying a hard
// roughness floor.
static const float IE_PBR_LOCAL_SOURCE_ROUGHNESS = 0.30f;

// Approximate the fraction of single-scatter GGX energy available for another
// bounce. The secondary lobe is conductor-tinted and capped independently so
// it cannot turn a metal into a white Lambert surface.
static const float IE_PBR_LOCAL_MULTISCATTER_ENERGY = 0.50f;
static const float IE_PBR_LOCAL_MULTISCATTER_LIMIT = 0.25f;

// Infernis PBR Stage 2.38: conductor transport calibration.
// 0 = baseline, 1 = bounded F0 presentation bridge,
// 2 = rough-conductor irradiance fill, 3 = both corrections.
//
// Stage 2.37 proved that merely widening the local GGX highlight cannot
// restore the missing body response. X-Ray's diffuse bridge presents diffuse
// Albedo in legacy display units, while conductor F0 and cubemap reflections
// remain linear. Keep the two suspected compensations independently testable.
#define IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE 0

// Blend only part-way toward the legacy presentation domain. The correction
// remains colored by authored conductor Albedo and cannot create a neutral
// white F0.
static const float IE_PBR_CONDUCTOR_F0_BRIDGE_STRENGTH = 0.35f;

// A mathematical point light and a dark reflection direction underrepresent
// the low-frequency response of a rough conductor in the classic renderer.
// These bounded weights reuse existing irradiance, remain F0-tinted, and never
// re-enable the dielectric Albedo diffuse lobe.
static const float IE_PBR_CONDUCTOR_LOCAL_FILL_SCALE = 0.45f;
static const float IE_PBR_CONDUCTOR_LOCAL_FILL_LIMIT = 0.20f;
static const float IE_PBR_CONDUCTOR_AMBIENT_FILL_SCALE = 0.65f;
static const float IE_PBR_CONDUCTOR_AMBIENT_FILL_LIMIT = 0.35f;

// Infernis PBR Stage 2.39: conductor energy-route reference audit.
// 0 = baseline, 1 = strong local-light reference,
// 2 = strong IBL reference, 3 = both reference sources.
//
// These deliberately obvious upper-bound modes distinguish missing incident
// radiance from a material/F0 error. They are calibration probes, not a
// production energy model.
#define IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE 0

// Roughness controls how much of the broad reference response is available.
// The can lid's authored roughness near 0.40 reaches roughly 80% of the probe,
// while smooth conductors retain their directional GGX character.
static const float IE_PBR_CONDUCTOR_REFERENCE_ROUGHNESS_GAIN = 2.0f;
static const float IE_PBR_CONDUCTOR_LOCAL_REFERENCE_STRENGTH = 1.0f;
static const float IE_PBR_CONDUCTOR_IBL_REFERENCE_STRENGTH = 1.0f;

// Infernis PBR Stage 2.42: shadow-aware HUD conductor transport audit.
// 0 = baseline, 1 = integrated local conductor response,
// 2 = suppress unoccluded sky IBL on near-camera conductors, 3 = both.
//
// The local term is evaluated inside the ordinary local-light pass, therefore
// X-Ray's spotlight mask, range attenuation, projected lightmap, and shadow
// factor still modulate it in accum_base.ps.  It is not ambient fill and it
// cannot illuminate a metal when the local light is hidden or switched off.
#define IE_PBR_STAGE242_HUD_CONDUCTOR_MODE 0

static const float IE_PBR_STAGE242_LOCAL_STRENGTH = 1.35f;
static const float IE_PBR_STAGE242_HUD_MAX_DISTANCE = 2.0f;
static const float IE_PBR_STAGE242_HUD_SKY_FLOOR = 0.0f;

// Infernis PBR Stage 2.46: local-light HUD projection bridge.
// R3/R4 reconstruct every G-buffer position with the world projection even
// though HUD geometry was rasterized with psHUD_FOV * Device.fFOV.  That sends
// the local GGX and its shadow lookup to different view-space coordinates.
// 0 = baseline, 1 = selected-pixel mask, 2 = corrected local-light position.
#define IE_PBR_STAGE246_LOCAL_HUD_PROJECTION 0

// Infernis PBR Stage 2.47: split the local-light result at the exact point
// where a PBR BRDF is multiplied by the projected shadow/lightmap factor.
// 0 = production, 1 = RGB split probe, 2 = bypass visibility on selected HUD.
#define IE_PBR_STAGE247_LOCAL_SPLIT_MODE 0

// Infernis PBR Stage 2.48: near-HUD omni conductor response.
// The actor torch intentionally uses a shadowed spotlight for the world and
// a short unshadowed point light for first-person fill.  Stage 2.47 proved
// that HUD items receive the latter route.  A delta point sample leaves a
// rough conductor with only sparse GGX pixels, so integrate a bounded part of
// the finite emitter footprint in accum_omni_unshadowed.ps only.
// 0 = baseline, 1 = balanced production response, 2 = stronger comparison.
#define IE_PBR_STAGE248_HUD_OMNI_CONDUCTOR_MODE 0

static const float IE_PBR_STAGE248_BALANCED_STRENGTH = 1.15f;
static const float IE_PBR_STAGE248_STRONG_STRENGTH = 1.35f;
static const float IE_PBR_STAGE248_OMNI_SOURCE_RADIUS = 0.35f;
static const float IE_PBR_STAGE248_MIN_ANGULAR_WIDTH = 0.30f;

// Infernis PBR Stage 2.50: local rough-conductor energy compensation.
// X-Ray's diffuse bridge presents dielectric Albedo in its established
// display domain, while metallic=1 removes that whole lobe and leaves only a
// sparse single-scatter GGX sample.  Restore the low-frequency part of rough
// conductor reflection at the BRDF split itself, using presented F0 rather
// than reintroducing dielectric diffuse Albedo.
// Stage 2.51 extends the verified reference with two calibration levels.
// Stage 2.52 adds the final 3.00x/3.50x candidates while retaining 2.50x.
// 0 = baseline, 1 = balanced, 2 = reference, 3 = boosted, 4 = high,
// 5 = higher, 6 = maximum.
#define IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE 5

static const float IE_PBR_STAGE250_BALANCED_STRENGTH = 0.72f;
static const float IE_PBR_STAGE250_REFERENCE_STRENGTH = 1.0f;
static const float IE_PBR_STAGE251_BOOSTED_STRENGTH = 1.75f;
static const float IE_PBR_STAGE251_HIGH_STRENGTH = 2.50f;
static const float IE_PBR_STAGE252_HIGHER_STRENGTH = 3.00f;
static const float IE_PBR_STAGE252_MAXIMUM_STRENGTH = 3.50f;

static const float IE_PBR_STAGE246_HUD_MAX_DEPTH = 0.85f;

uniform float4 ie_pbr_hud_projection_params;

float ie_pbr_stage246_hud_weight(gbuffer_data gbd)
{
    return
        (gbd.pbr > 0.5f &&
         gbd.P.z > ie_pbr_hud_projection_params.z &&
         gbd.P.z < IE_PBR_STAGE246_HUD_MAX_DEPTH) ?
        1.0f :
        0.0f;
}

void ie_pbr_stage246_correct_local_position(inout gbuffer_data gbd)
{
#if IE_PBR_STAGE246_LOCAL_HUD_PROJECTION == 2
    float hudWeight = ie_pbr_stage246_hud_weight(gbd);
    gbd.P.xy =
        lerp(
            gbd.P.xy,
            gbd.P.xy * ie_pbr_hud_projection_params.xx,
            hudWeight
        );
#endif
}

float ie_pbr_stage247_luminance(float3 value)
{
    return dot(
        max(value, float3(0.0f, 0.0f, 0.0f)),
        float3(0.2126f, 0.7152f, 0.0722f)
    );
}


// ------------------------------------------------------------
// Infernis PBR Stage 2.6: private gamma-to-linear transport.
//
// USE_GAMMA_22 is disabled in the classic shader pack. Enabling it globally
// would also alter every legacy material, so sampled PBR colors own their
// conversion here without touching legacy materials.
//
// Infernis PBR Stage 2.16: separate color and lighting domains.
// Diffuse albedo and the existing environment maps keep their established
// gamma decode. Ldynamic_color is already an HDR light value and must preserve
// values above 1.0; pow(value, 2.2) can otherwise amplify 2.0 into 4.59 and
// 4.0 into 21.1 before the classic FP16 accumulator. Shadow, attenuation, and
// lightmap modulation are linear visibility factors and must not be decoded.
// ------------------------------------------------------------

static const float IE_PBR_GAMMA = 2.2f;
static const float IE_PBR_INV_GAMMA = 0.45454545f;

// Infernis PBR Stage 2.28:
// The classic X-Ray combine/tonemap path keeps diffuse textures and diffuse
// environment lighting in its established presentation domain. A private
// gamma decode without a matching encode crushed rough dielectric materials.
// Keep this bridge switchable while metallic F0 and specular IBL stay linear.
#define IE_PBR_XRAY_DIFFUSE_BRIDGE 1

// Infernis PBR Stage 2.29: independent energy calibration.
// The balanced preset trims only the diffuse energy that Stage 2.28 restored.
// Specular remains at the proven Stage 2.24/2.27 response.
static const float IE_PBR_CAL_SUN_DIFFUSE = 0.90f;
static const float IE_PBR_CAL_SUN_SPECULAR = 1.00f;
static const float IE_PBR_CAL_LOCAL_DIFFUSE = 0.80f;
static const float IE_PBR_CAL_LOCAL_SPECULAR = 1.00f;
static const float IE_PBR_CAL_AMBIENT_DIFFUSE = 0.90f;
static const float IE_PBR_CAL_AMBIENT_SPECULAR = 1.00f;

// Infernis PBR Stage 2.30: controlled material-response calibration.
// 0 = authored material, 1 = rough dielectric, 2 = smooth dielectric,
// 3 = rough metal, 4 = smooth metal.
//
// This is a lighting-only override: G-buffer diagnostics keep showing the
// authored ORM values, so the calibration cannot hide a packing error.
#define IE_PBR_MATERIAL_OVERRIDE_MODE 0

void ie_pbr_apply_material_override(
    inout float metallic,
    inout float roughness
)
{
#if IE_PBR_MATERIAL_OVERRIDE_MODE == 1
    metallic = 0.0f;
    roughness = 0.85f;
#elif IE_PBR_MATERIAL_OVERRIDE_MODE == 2
    metallic = 0.0f;
    roughness = 0.20f;
#elif IE_PBR_MATERIAL_OVERRIDE_MODE == 3
    metallic = 1.0f;
    roughness = 0.75f;
#elif IE_PBR_MATERIAL_OVERRIDE_MODE == 4
    metallic = 1.0f;
    roughness = 0.15f;
#endif
}

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

float3 ie_pbr_prepare_diffuse_albedo(float3 linearAlbedo)
{
#if IE_PBR_XRAY_DIFFUSE_BRIDGE
    return pow(
        saturate(linearAlbedo),
        IE_PBR_INV_GAMMA
    );
#else
    return linearAlbedo;
#endif
}

// Environment cubemaps keep the Stage 2.9 transport until their sampler color
// space is validated independently.
float3 ie_pbr_prepare_radiance(float3 radiance)
{
    return ie_pbr_to_linear(radiance);
}

float3 ie_pbr_prepare_specular_radiance(float3 radiance)
{
    // Stage 2.31 verified that the environment cubemap belongs to the linear
    // specular transport path used by the PBR BRDF.
    return ie_pbr_prepare_radiance(radiance);
}

float3 ie_pbr_prepare_diffuse_radiance(float3 radiance)
{
#if IE_PBR_XRAY_DIFFUSE_BRIDGE
    return max(
        radiance,
        float3(0.0f, 0.0f, 0.0f)
    );
#else
    return ie_pbr_prepare_radiance(radiance);
#endif
}

// Dynamic sun/local colors already represent radiance and may be HDR.
float3 ie_pbr_prepare_dynamic_radiance(float3 radiance)
{
    return max(
        radiance,
        float3(0.0f, 0.0f, 0.0f)
    );
}

float3 ie_pbr_prepare_light_factor(float3 factor)
{
    return saturate(factor);
}

float ie_pbr_prepare_light_factor(float factor)
{
    return saturate(factor);
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

float3 ie_pbr_resolve_f0(
    float3 albedo,
    float metallic
)
{
    float3 conductorF0 = saturate(albedo);

#if IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE == 1 || IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE == 3
    // Stage 2.28 must bridge diffuse materials back to X-Ray's established
    // presentation domain. Test a deliberately partial version of that bridge
    // for conductor F0 instead of applying a global gamma change.
    float3 presentedConductorF0 =
        pow(
            conductorF0,
            IE_PBR_INV_GAMMA
        );

    conductorF0 =
        lerp(
            conductorF0,
            presentedConductorF0,
            IE_PBR_CONDUCTOR_F0_BRIDGE_STRENGTH
        );
#endif

    float3 F0 =
        lerp(
            float3(0.04f, 0.04f, 0.04f),
            conductorF0,
            metallic
        );

#if IE_PBR_MATERIAL_OVERRIDE_MODE == 3 || IE_PBR_MATERIAL_OVERRIDE_MODE == 4
    // Stage 2.30 forces a stone material to behave as metal. Its original dark
    // Albedo is not a useful conductor F0 and caused the rough-metal black-hole
    // result. Use a moderate neutral linear reflectance for calibration only;
    // authored materials remain untouched when the override is off.
    F0 = float3(0.22f, 0.22f, 0.22f);
#endif

    return F0;
}


// ------------------------------------------------------------
// Direct PBR BRDF
//
// N = surface normal
// V = surface -> camera
// L = surface -> light
// ------------------------------------------------------------

// Infernis PBR Stage 2.17:
// Expose the diffuse and GGX/Fresnel lobes independently. Their sum is
// algebraically identical to the previous lerp(diffuse, specular, F) result,
// but the directional-sun path can now isolate either lobe for diagnosis.
void ie_pbr_direct_brdf_lobes(
    float3 albedo,
    float metallic,
    float roughness,
    float3 N,
    float3 V,
    float3 L,
    float directSpecularLimit,
    float localLight,
    out float3 diffuseLobe,
    out float3 specularLobe
)
{
    diffuseLobe = float3(0.0f, 0.0f, 0.0f);
    specularLobe = float3(0.0f, 0.0f, 0.0f);

    ie_pbr_apply_material_override(
        metallic,
        roughness
    );

    metallic = saturate(metallic);

    roughness =
        clamp(
            roughness,
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );

    float materialRoughness = roughness;
    float localLightWeight = saturate(localLight);

#if IE_PBR_STAGE237_LOCAL_METAL_MODE == 2 || IE_PBR_STAGE237_LOCAL_METAL_MODE == 3
    float sourceRoughness =
        IE_PBR_LOCAL_SOURCE_ROUGHNESS *
        metallic *
        localLightWeight;

    roughness =
        clamp(
            sqrt(
                roughness * roughness +
                sourceRoughness * sourceRoughness
            ),
            IE_PBR_MIN_ROUGHNESS,
            1.0f
        );
#endif

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
        return;
    }

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

    // Standard dielectric F0. Metals get their F0 from Albedo.
    float3 F0 =
        ie_pbr_resolve_f0(
            albedo,
            metallic
        );

    float3 F =
        ie_pbr_fresnel_schlick(
            F0,
            HdotV
        );

    float specularValue =
        min(
            D * G,
            max(
                directSpecularLimit,
                IE_PBR_EPSILON
            )
        );

    float3 diffuse =
        ie_pbr_prepare_diffuse_albedo(albedo) *
        (1.0f - metallic) *
        IE_PBR_INV_PI;

    // diffuse*(1-F) + specular*F, each with the shared cosine term.
    diffuseLobe =
        diffuse *
        (1.0f - F) *
        NdotL;

    specularLobe =
        float3(
            specularValue,
            specularValue,
            specularValue
        ) *
        F *
        NdotL;

#if IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE >= 1 && IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE <= 6
    // This is a broad specular return, not a metallic diffuse leak.  F0 keeps
    // the reflected colour authored by the conductor Albedo; roughness
    // controls how much energy escapes the unresolved single GGX direction;
    // localLight guarantees that only real dynamic local-light radiance can
    // feed it.  The shared NdotL still preserves surface orientation.
    float conductorEnergyCoverage =
        saturate(
            materialRoughness *
            IE_PBR_CONDUCTOR_REFERENCE_ROUGHNESS_GAIN
        ) *
        metallic *
        localLightWeight;

    float3 presentedF0 =
        ie_pbr_prepare_diffuse_albedo(F0);

#if IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE == 6
    float conductorEnergyStrength =
        IE_PBR_STAGE252_MAXIMUM_STRENGTH;
#elif IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE == 5
    float conductorEnergyStrength =
        IE_PBR_STAGE252_HIGHER_STRENGTH;
#elif IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE == 4
    float conductorEnergyStrength =
        IE_PBR_STAGE251_HIGH_STRENGTH;
#elif IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE == 3
    float conductorEnergyStrength =
        IE_PBR_STAGE251_BOOSTED_STRENGTH;
#elif IE_PBR_STAGE250_LOCAL_CONDUCTOR_ENERGY_MODE == 2
    float conductorEnergyStrength =
        IE_PBR_STAGE250_REFERENCE_STRENGTH;
#else
    float conductorEnergyStrength =
        IE_PBR_STAGE250_BALANCED_STRENGTH;
#endif

    specularLobe +=
        presentedF0 *
        conductorEnergyCoverage *
        conductorEnergyStrength *
        IE_PBR_INV_PI *
        NdotL;
#endif

#if IE_PBR_STAGE237_LOCAL_METAL_MODE == 1 || IE_PBR_STAGE237_LOCAL_METAL_MODE == 3
    // Kulla-Conty-inspired broad conductor return. Favg is Schlick Fresnel
    // averaged over the hemisphere. This is still specular energy: it is
    // metallic/F0 tinted, has no Albedo diffuse fallback, and vanishes for
    // dielectric materials and non-local lights.
    float missingEnergy =
        saturate(
            materialRoughness *
            materialRoughness *
            IE_PBR_LOCAL_MULTISCATTER_ENERGY
        ) *
        metallic *
        localLightWeight;

    float3 Favg =
        saturate(
            F0 +
            (1.0f - F0) * (1.0f / 21.0f)
        );

    float3 multiScatterWeight =
        Favg *
        Favg *
        missingEnergy /
        max(
            1.0f - Favg * missingEnergy,
            float3(
                IE_PBR_EPSILON,
                IE_PBR_EPSILON,
                IE_PBR_EPSILON
            )
        );

    float3 multiScatterLobe =
        multiScatterWeight *
        IE_PBR_INV_PI *
        NdotL;

    specularLobe +=
        min(
            multiScatterLobe,
            F0 * IE_PBR_LOCAL_MULTISCATTER_LIMIT * NdotL
        );
#endif

#if IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE == 2 || IE_PBR_STAGE238_CONDUCTOR_TRANSPORT_MODE == 3
    // Low-frequency local conductor response. This approximates the angular
    // footprint lost when X-Ray represents the flashlight as one delta-light
    // sample. It is specular energy: F0 tinted, roughness dependent, and zero
    // for dielectric materials, the sun, and non-local BRDF calls.
    float conductorLocalFillWeight =
        min(
            materialRoughness * IE_PBR_CONDUCTOR_LOCAL_FILL_SCALE,
            IE_PBR_CONDUCTOR_LOCAL_FILL_LIMIT
        ) *
        metallic *
        localLightWeight;

    specularLobe +=
        F0 *
        conductorLocalFillWeight *
        IE_PBR_INV_PI *
        NdotL;
#endif

#if IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE == 1 || IE_PBR_STAGE239_CONDUCTOR_REFERENCE_MODE == 3
    // Strong local reference: approximate the integrated footprint that a
    // finite flashlight source would contribute across a rough conductor.
    // It remains specular transport because the energy is conductor-F0
    // colored, has no Albedo diffuse term, and is local-light-only.
    float conductorReferenceWeight =
        saturate(
            materialRoughness *
            IE_PBR_CONDUCTOR_REFERENCE_ROUGHNESS_GAIN
        ) *
        metallic *
        localLightWeight;

    specularLobe +=
        F0 *
        conductorReferenceWeight *
        IE_PBR_CONDUCTOR_LOCAL_REFERENCE_STRENGTH *
        IE_PBR_INV_PI *
        NdotL;
#endif

#if IE_PBR_STAGE242_HUD_CONDUCTOR_MODE == 1 || IE_PBR_STAGE242_HUD_CONDUCTOR_MODE == 3
    // A rough conductor reflects a finite local emitter over a wider angular
    // domain than the single delta direction available in classic X-Ray.
    // Integrate a conservative low-frequency part of that specular response.
    // F0 supplies the authored metal colour; metallic/localLight make the term
    // vanish for paper, legacy calls, the sun, and ambient lighting.
    float localConductorCoverage =
        saturate(materialRoughness * materialRoughness * 2.0f) *
        metallic *
        localLightWeight;

    specularLobe +=
        F0 *
        localConductorCoverage *
        IE_PBR_STAGE242_LOCAL_STRENGTH *
        IE_PBR_INV_PI *
        NdotL;
#endif
}

float3 ie_pbr_stage248_hud_omni_conductor_return(
    gbuffer_data gbd,
    float3 lightPosition,
    float lightRangeRsq
)
{
#if IE_PBR_STAGE248_HUD_OMNI_CONDUCTOR_MODE == 0
    return float3(0.0f, 0.0f, 0.0f);
#else
    float hudWeight = ie_pbr_stage246_hud_weight(gbd);
    if (hudWeight <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float metallic = gbd.metallic;
    float roughness = gbd.roughness;
    ie_pbr_apply_material_override(metallic, roughness);

    metallic = saturate(metallic);
    roughness = clamp(roughness, IE_PBR_MIN_ROUGHNESS, 1.0f);
    if (metallic <= IE_PBR_EPSILON)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 lightToPoint = gbd.P - lightPosition;
    float distanceSq = dot(lightToPoint, lightToPoint);
    float attenuation = ie_pbr_prepare_light_factor(
        saturate(1.0f - distanceSq * lightRangeRsq)
    );

    float distanceToLight = sqrt(max(distanceSq, IE_PBR_EPSILON));
    float3 L = -lightToPoint / distanceToLight;
    float pointNdotL = dot(normalize(gbd.N), L);

    // Stage 2.49: integrate the cosine response over a finite spherical
    // emitter.  The original delta sample clamps pointNdotL to zero across
    // most of a near-camera lid; only normal-map outliers survive as dots.
    // A finite source crosses that geometric horizon and contributes over a
    // continuous patch, as a real flashlight reflector does.
    float angularWidth =
        max(
            IE_PBR_STAGE248_MIN_ANGULAR_WIDTH,
            saturate(
                IE_PBR_STAGE248_OMNI_SOURCE_RADIUS /
                distanceToLight
            )
        );
    float finiteEmitterNdotL =
        saturate(
            (pointNdotL + angularWidth) /
            (1.0f + angularWidth)
        );

    if (finiteEmitterNdotL <= IE_PBR_EPSILON || attenuation <= IE_PBR_EPSILON)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 albedo = ie_pbr_prepare_albedo(gbd.C);
    float3 F0 = ie_pbr_resolve_f0(albedo, metallic);
    float emitterCoverage =
        saturate(roughness * IE_PBR_CONDUCTOR_REFERENCE_ROUGHNESS_GAIN) *
        metallic;

#if IE_PBR_STAGE248_HUD_OMNI_CONDUCTOR_MODE == 2
    float strength = IE_PBR_STAGE248_STRONG_STRENGTH;
#else
    float strength = IE_PBR_STAGE248_BALANCED_STRENGTH;
#endif

    // This remains reflected local-light energy, not emissive or diffuse:
    // authored conductor F0 supplies its colour and the real omni light still
    // supplies direction, range attenuation, radiance, and on/off state.
    return
        F0 *
        emitterCoverage *
        strength *
        IE_PBR_INV_PI *
        finiteEmitterNdotL *
        attenuation *
        hudWeight;
#endif
}

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
    float3 diffuseLobe;
    float3 specularLobe;

    ie_pbr_direct_brdf_lobes(
        albedo,
        metallic,
        roughness,
        N,
        V,
        L,
        directSpecularLimit,
        0.0f,
        diffuseLobe,
        specularLobe
    );

    return diffuseLobe + specularLobe;
}

#endif
