/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 17
 * @ Description: HUD Raindrops - Base
 * @ Modified time: 2023-08-04 07:23
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

Texture2D s_hud_rain; // Raindrops texture [ x: Fade | y: Normal-Y | z: Normal-X | w: Time Offset ]

#define SSFX_HUD_LIGHTVECTOR	float3(0.30f, 0.59f, 0.11f) // Vector to adjust the intensity of the raindrops at night

uniform float4 ssfx_hud_drops_1; // Time, Int, refle, refra
uniform float4 ssfx_hud_drops_2; // Density, Size, Extra Gloss, Gloss

// DEVIATION FROM THE DONOR, asked for.
//
// The donor scales the drops by saturate( world-space normal Y ), so only a face pointing straight
// up collects anything: the sides of the receiver are at exactly zero and the underside of the
// barrel, the magazine and the grip are below zero and get nothing at all. Rain does not stop at
// the top of a held object - it runs round it and hangs underneath - so the weapon ended up with a
// thin wet stripe along its top and nothing anywhere else.
//
// This is the same curve, wrapped: remap the normal from [-1..1] to [0..1] and lerp from a floor
// instead of from zero. Straight up keeps the donor's full value, a vertical face keeps
// SSFX_HUD_DROPS_SIDE, and the underside keeps SSFX_HUD_DROPS_UNDER. Monotonic, so the top is
// still clearly the wettest part, and it costs one mad.
#define SSFX_HUD_DROPS_UNDER 0.20f // underside, normal Y = -1
#define SSFX_HUD_DROPS_SIDE  0.60f // vertical face, normal Y = 0 ( must sit between the two )

float ssfx_hud_drops_facing(float normal_y)
{
    float up = saturate(normal_y * 0.5f + 0.5f);

    // Two straight segments so the side value is exactly SSFX_HUD_DROPS_SIDE at normal Y = 0.
    return up < 0.5f
        ? lerp(SSFX_HUD_DROPS_UNDER, SSFX_HUD_DROPS_SIDE, up * 2.0f)
        : lerp(SSFX_HUD_DROPS_SIDE, 1.0f, (up - 0.5f) * 2.0f);
}

float3 ssfx_process_drops(float4 droplets, float speed, float intensity )
{
	// Unpack normal [ -1.0 ~ 1.0 ]
	float2 droplets_N = droplets.yz * 2 - 1;

	// Apply time
	float DropsTime = ssfx_hud_drops_1.x * speed; // Fade * speed factor
	float DropsFrac = 1.0f - frac(droplets.w + DropsTime); // Time offset
	float DropsInt = saturate((DropsFrac * droplets.x) - ssfx_hud_drops_2.x); // (Time * Fade) - Density

	// Apply intensity
	DropsInt *= intensity;
	droplets_N *= DropsInt;

	return float3(droplets_N, DropsInt);
}

// Mapping based on https://iquilezles.org/articles/biplanar/
// Thanks to the chaotic nature of the drops, we can do a very basic planar mapping with 1 sample ( I'm using 2 to add variety to the drops )
float3 ssfx_hud_raindrops( Texture2D drops_tex, float3 p, float uv_scale )
{
	// Partial derivatives to reconstruct normal & mapping
	float3 d1 = ddx(p);
	float3 d2 = ddy(p);
		
	// Recontruct normal
	float3 n = abs(normalize(cross(d1, d2)));

	// Major axis (in x; yz are following axis)
	int3 ma = (n.x > n.y && n.x > n.z)	? int3(0, 1, 2) : // [ yz ] Side
			  (n.y > n.z)				? int3(1, 2, 0) : // [ zx ] Top
										  int3(2, 0, 1) ; // [ xy ] Front 

	// Uvs & partial derivatives
	float2 uvs = float2(p[ma.y], p[ma.z]) * ssfx_hud_drops_2.y;
	float4 d1_d2 = float4(d1[ma.y], d1[ma.z], d2[ma.y], d2[ma.z]);

	// Project + Fetch [ x: Fade | y: Normal-Y | z: Normal-X | w: Time Offset ]
	float4 Layer0 = drops_tex.SampleGrad(smp_base, uvs * 15.0f, d1_d2.xy, d1_d2.zw);
	float4 Layer1 = drops_tex.SampleGrad(smp_base, (uvs + float2(.23,.46)) * 8.0f, d1_d2.xy, d1_d2.zw);

	float w = pow( n[ma.x], 5 );

	// Process animation
	float3 result = ssfx_process_drops(Layer0, 0.2f, w) + ssfx_process_drops(Layer1, 0.1f, w);
	result.xy = clamp(result.xy, -1.0f, 1.0f);

	return result;
}