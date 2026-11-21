// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#ifndef SLOAD_H
#define SLOAD_H

#include "common.h"
#include "check_screenspace.h"

// Screen Space Shaders - parallax occlusion mapping. The donor declares this unconditionally and
// has no SSFX_POM switch anywhere: its parallax occlusion replaces the stock steep parallax, so
// the stock 25-step version that used to sit behind #else below is gone, as in the donor.
uniform float4 ssfx_pom; // Samples, Range, Height, AO

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5),
											   float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct surface_bumped
{
	float4 base;
	float3 normal;

	// Legacy X-Ray value
	float gloss;

	// Height / virtual height
	float height;

	// PBR data
	float metallic;
	float roughness;
	float ao;
	float pbr_reserved;
};

// Screen Space Shaders: the donor's two normal blend helpers, copied unchanged. NormalBlend is
// what the detail-bump path below uses; NormalBlend_Reoriented is declared in the donor too and
// kept here for the same reason - other SSFX files may reach for it, and an unused function costs
// nothing once the shader is compiled.
float3 NormalBlend(float3 A, float3 B)
{
	return normalize(float3(A.rg + B.rg, A.b * B.b));
}

float3 NormalBlend_Reoriented(float3 A, float3 B)
{
	float3 t = A.xyz + float3(0.0, 0.0, 1.0);
	float3 u = B.xyz * float3(-1.0, -1.0, 1.0);
	return (t / t.z) * dot(t, u) - u;
}

float4 tbase(float2 tc)
{
	return	s_base.Sample(smp_base, tc);
}

float sample_height(float2 tc)
{
#ifdef USE_PBR
	// PBR:
	// _bump.R = Height
	return s_bump.Sample(smp_base, tc).r;
#else
	// Legacy X-Ray:
	// _bump#.A = Height
	return s_bumpX.Sample(smp_base, tc).a;
#endif
}

float sample_height_lod0(float2 tc)
{
#ifdef USE_PBR
	return s_bump.SampleLevel(smp_base, tc, 0).r;
#else
	return s_bumpX.SampleLevel(smp_base, tc, 0).a;
#endif
}

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

/**
 * @ Description: POM Shader - Modified version from the terrain POM implementation
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
*/
// Deviation from the donor: it samples the height straight out of s_bumpX.a, which is the legacy
// texture layout only. Going through sample_height_lod0 picks the channel that matches whichever
// layout the surface was compiled for, so this works on PBR materials as well.
void UpdateTC(inout p_bumped I)
{
	// Distance attenuation
	float dist_att = 1.0f - smoothstep(ssfx_pom.y * 0.8f, ssfx_pom.y, I.position.z);

	if (I.position.z >= ssfx_pom.y)
		return;

	float3 eye = normalize(mul(float3x3(I.M1.x, I.M2.x, I.M3.x,
		I.M1.y, I.M2.y, I.M3.y,
		I.M1.z, I.M2.z, I.M3.z), -I.position.xyz));

	float view_angle = abs(dot(float3(0.0, 0.0, 1.0), eye));

	// Dynamic steps
	float _step = rcp(lerp(ssfx_pom.x, 3, view_angle));
	_step *= 0.5; // We are working with 0.0 ~ 0.5

	// View direction + bias to try to minimize issues with some slopes.
	float2 viewdir = eye.xy / (abs(eye.z) + 0.41f);

	// Offset direction
	float2 tc_step = _step * viewdir * ssfx_pom.z * dist_att;

	// Init vars to store steps
	float curr_step = 0;
	float2 parallax_tc = I.tcdh;

	// Store the previous & current sample to do the [POM] calc and [Contact Refinement] if needed.
	float prev_Height = 0;
	float curr_Height = 0.5f;

	do // Step Parallax
	{
		// Save previous data
		prev_Height = curr_Height;

		// Step TexCoor
		parallax_tc -= tc_step;
		curr_step += _step;

		// Sample
		curr_Height = 0.5f - sample_height_lod0(parallax_tc);

	} while (curr_Height >= curr_step);

	// [ Contact Refinement ]
	#ifdef SSFX_POM_REFINE

		// Step back
		parallax_tc += tc_step;
		curr_step -= _step;
		curr_Height = prev_Height; // Previous height

		// Increase precision ( 3 times seems like a good balance )
		_step /= 3;
		tc_step /= 3;

		do // Step Parallax
		{
			// Save previous data ( Used for interpolation )
			prev_Height = curr_Height;

			// Step TexCoor
			parallax_tc -= tc_step;
			curr_step += _step;

			// Sample
			curr_Height = 0.5f - sample_height_lod0(parallax_tc);

		} while (curr_Height >= curr_step);

	#endif

	// [ POM ] Interpolation between the previous offset and the current offset
	float currentDiff = curr_Height - curr_step;
	float ratio = currentDiff / (currentDiff - saturate(prev_Height - curr_step + _step));

	// Final TexCoor
	float2 final_tc = lerp(parallax_tc, parallax_tc + tc_step, ratio);

	// Apply Parallax TC
	I.tcdh.xy = final_tc;

#if defined(USE_TDETAIL)
	I.tcdbump = final_tc * dt_params; // Apply detail_scaler
#endif
}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC(inout p_bumped I)
{
	float3	 eye = mul(float3x3(I.M1.x, I.M2.x, I.M3.x,
		I.M1.y, I.M2.y, I.M3.y,
		I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

	float height = sample_height(I.tcdh);
	height = height * (parallax.x) + (parallax.y);	//
	float2	new_tc = I.tcdh + height * normalize(eye);	//

	//	Output the result
	// Assigning to I.tcdh whole does not compile: with USE_R2_STATIC_SUN and without USE_LM_HEMI
	// p_bumped.tcdh is a float4 whose w carries the sun occlusion, so a float2 cannot be written
	// over it and the w term would be lost anyway. The donor writes .xy here; same fix, and it is
	// what the SSFX_POM branch above already does.
	I.tcdh.xy = new_tc;

	// The donor also carries the parallax offset into the detail coordinates. Without this the
	// detail layer stays put while the base layer shifts, which is visible on every parallax
	// surface that has a detail texture.
	#if defined(USE_TDETAIL)
		I.tcdbump = new_tc * dt_params; // Apply detail_scaler
	#endif
}

#else	//	USE_PARALLAX

void UpdateTC(inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

void decode_bump(
	float4 Nu,
	float4 NuE,
	inout surface_bumped S
)
{
#ifdef USE_PBR

	// ------------------------------------------------
	// PBR texture layout
	//
	// _bump:
	// R = Height
	// G/B/A = Normal
	//
	// _bump#:
	// R = Metallic
	// G = Roughness
	// B = Reserved
	// A = AO
	// ------------------------------------------------

	// Existing X-Ray packing:
	// A = Normal X
	// B = Normal Y
	//
	// Reconstruct Z so _bump# is no longer needed
	// for normal correction.
	float2 normalXY = Nu.wz * 2.0f - 1.0f;

	S.normal.xy = normalXY;
	S.normal.z = sqrt(
		saturate(
			1.0f - dot(normalXY, normalXY)
		)
	);

	S.metallic = saturate(NuE.r);
	S.roughness = saturate(NuE.g);
	S.pbr_reserved = NuE.b;
	S.ao = saturate(NuE.a);

	// Temporary bridge for old lighting.
	// Later gloss disappears from the PBR lighting path.
	S.gloss = 1.0f - S.roughness;

	// Actual PBR height map.
	S.height = Nu.r;

#else

	// Screen Space Shaders: the donor decodes the normal as a plain 2x-1 and does not touch the
	// normal-error texture at all. This is the legacy, non-PBR path only - the PBR branch above is
	// this engine's own and stays exactly as it was. S.height keeps reading NuE.z because this
	// engine's G-buffer packing needs it there; the donor carries the height out of its parallax
	// step instead, which this file does not do.
	S.normal = unpack_normal( Nu.wzy );
	// The pseudo-PBR module reads the gloss map linearly, as the donor does ( sload.h:193-197 ).
#ifdef SSFX_ENHANCED_SHADERS
	S.gloss = Nu.x; // ES Style
#else
	S.gloss = Nu.x * Nu.x; // Vanilla Style
#endif
	S.height = NuE.z;

	S.metallic = 0.0f;
	S.roughness = 1.0f - S.gloss;
	S.ao = 1.0f;
	S.pbr_reserved = 0.0f;

#endif
}

surface_bumped sload_i(p_bumped I)
{
	surface_bumped	S;

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 Nu =
		s_bump.Sample(smp_base, I.tcdh);

	float4 NuE =
		s_bumpX.Sample(smp_base, I.tcdh);

	S.base = tbase(I.tcdh);

	decode_bump(Nu, NuE, S);
	//S.height	= 0;

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)
	// Screen Space Shaders - parallax self-occlusion.
	// The donor carries the height out of UpdateTC through an extra out parameter. Keeping the
	// stock UpdateTC signature costs one more fetch here instead, and sample_height() reads the
	// channel that matches the active texture layout.
	S.base.rgb *= lerp(1.0f, saturate((sample_height(I.tcdh) + 0.15f) * 2.0f), ssfx_pom.w);
#endif

#ifdef USE_TDETAIL

	float4 detail = s_detail.Sample(smp_base, I.tcdbump);
	S.base.rgb = S.base.rgb * detail.rgb * 2;

#ifdef USE_TDETAIL_BUMP

	float4 NDetail =
		s_detailBump.Sample(smp_base, I.tcdbump);

	float4 NDetailX =
		s_detailBumpX.Sample(smp_base, I.tcdbump);

	S.gloss =
		S.gloss * NDetail.x * 2;

	// Screen Space Shaders: the donor blends the detail normal into the base normal instead of
	// adding it, which is what stops the detail layer from flattening the surface. Legacy path
	// only - on PBR surfaces this engine's own addition stays untouched.
#ifdef USE_PBR
	S.normal +=
		NDetail.wzy +
		NDetailX.xyz -
		1.0h;
#else
	float3 DetailNormal = unpack_normal(NDetail.wzy) + unpack_normal(NDetailX.xyz);
	S.normal = NormalBlend(S.normal, DetailNormal);
#endif

#else

	S.gloss =
		S.gloss * detail.w * 2;

#endif

#endif // USE_TDETAIL

	return S;
}

surface_bumped sload_i(p_bumped I, float2 pixeloffset)
{
	surface_bumped	S;

	// apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
	I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu = s_bump.Sample(smp_base, I.tcdh);		// IN:	normal.gloss
	float4 	NuE = s_bumpX.Sample(smp_base, I.tcdh);	// IN:	normal_error.height

	S.base = tbase(I.tcdh);				//	IN:  rgb.a
	decode_bump(Nu, NuE, S);
	//S.height	= 0;

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)
	// Screen Space Shaders - parallax self-occlusion. See the note in the other sload_i overload.
	S.base.rgb *= lerp(1.0f, saturate((sample_height(I.tcdh) + 0.15f) * 2.0f), ssfx_pom.w);
#endif

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
	I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail = s_detailBump.Sample(smp_base, I.tcdbump);
	float4 NDetailX = s_detailBumpX.Sample(smp_base, I.tcdbump);
	S.gloss = S.gloss * NDetail.x * 2;
	//S.normal			+= NDetail.wzy-.5;
	// Screen Space Shaders: same detail-normal blend as the other overload, legacy path only.
#ifdef USE_PBR
	S.normal += NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)
#else
	float3 DetailNormal = unpack_normal(NDetail.wzy) + unpack_normal(NDetailX.xyz);
	S.normal = NormalBlend(S.normal, DetailNormal);
#endif

	float4 detail = s_detail.Sample(smp_base, I.tcdbump);
	S.base.rgb = S.base.rgb * detail.rgb * 2;

	//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
	I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
	float4 detail = s_detail.Sample(smp_base, I.tcdbump);
	S.base.rgb = S.base.rgb * detail.rgb * 2;
	S.gloss = S.gloss * detail.w * 2;
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}


surface_bumped sload(p_bumped I)
{
	surface_bumped      S = sload_i(I);
#ifndef USE_PBR
	S.normal.z *= 0.5;
#endif

#ifdef	GBUFFER_OPTIMIZATION
	S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
	return              S;
}

surface_bumped sload(p_bumped I, float2 pixeloffset)
{
	surface_bumped      S = sload_i(I, pixeloffset);
#ifndef USE_PBR
	S.normal.z *= 0.5;
#endif
#ifdef	GBUFFER_OPTIMIZATION
	S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
	return              S;
}

#endif
#else
#ifndef SLOAD_H
#define SLOAD_H

#include "common.h"

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5),
											   float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct surface_bumped
{
	float4 base;
	float3 normal;

	// Legacy X-Ray value
	float gloss;

	// Height / virtual height
	float height;

	// PBR data
	float metallic;
	float roughness;
	float ao;
	float pbr_reserved;
};

float4 tbase(float2 tc)
{
	return	s_base.Sample(smp_base, tc);
}

float sample_height(float2 tc)
{
#ifdef USE_PBR
	// PBR:
	// _bump.R = Height
	return s_bump.Sample(smp_base, tc).r;
#else
	// Legacy X-Ray:
	// _bump#.A = Height
	return s_bumpX.Sample(smp_base, tc).a;
#endif
}

float sample_height_lod0(float2 tc)
{
#ifdef USE_PBR
	return s_bump.SampleLevel(smp_base, tc, 0).r;
#else
	return s_bumpX.SampleLevel(smp_base, tc, 0).a;
#endif
}

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

static const float fParallaxStartFade = 8.0f;
static const float fParallaxStopFade = 12.0f;

void UpdateTC(inout p_bumped I)
{
	if (I.position.z < fParallaxStopFade)
	{
		const float maxSamples = 25;
		const float minSamples = 5;
		const float fParallaxOffset = -0.013;

		float3	 eye = mul(float3x3(I.M1.x, I.M2.x, I.M3.x,
			I.M1.y, I.M2.y, I.M3.y,
			I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

		eye = normalize(eye);

		//	Calculate number of steps
		float nNumSteps = lerp(maxSamples, minSamples, eye.z);

		float	fStepSize = 1.0 / nNumSteps;
		float2	vDelta = eye.xy * fParallaxOffset * 1.2;
		float2	vTexOffsetPerStep = fStepSize * vDelta;

		//	Prepare start data for cycle
		float2	vTexCurrentOffset = I.tcdh;
		float	fCurrHeight = 0.0;
		float	fCurrentBound = 1.0;

		for (int i = 0; i < nNumSteps; ++i)
		{
			if (fCurrHeight < fCurrentBound)
			{
				vTexCurrentOffset += vTexOffsetPerStep;
				fCurrHeight = sample_height_lod0(vTexCurrentOffset.xy);
				fCurrentBound -= fStepSize;
			}
		}

		/*
				[unroll(25)]	//	Doesn't work with [loop]
				for( ;fCurrHeight < fCurrentBound; fCurrentBound -= fStepSize )
				{
					vTexCurrentOffset += vTexOffsetPerStep;
					fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a;
				}
		*/
		//	Reconstruct previouse step's data
		vTexCurrentOffset -= vTexOffsetPerStep;
		float fPrevHeight = sample_height(vTexCurrentOffset.xy);

		//	Smooth tc position between current and previouse step
		float	fDelta2 = ((fCurrentBound + fStepSize) - fPrevHeight);
		float	fDelta1 = (fCurrentBound - fCurrHeight);
		float	fParallaxAmount = (fCurrentBound * fDelta2 - (fCurrentBound + fStepSize) * fDelta1) / (fDelta2 - fDelta1);
		float	fParallaxFade = smoothstep(fParallaxStopFade, fParallaxStartFade, I.position.z);
		float2	vParallaxOffset = vDelta * ((1 - fParallaxAmount) * fParallaxFade);
		float2	vTexCoord = I.tcdh + vParallaxOffset;

		//	Output the result
		I.tcdh = vTexCoord;

#if defined(USE_TDETAIL) && defined(USE_STEEPPARALLAX)
		I.tcdbump = vTexCoord * dt_params;
#endif
	}

}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC(inout p_bumped I)
{
	float3	 eye = mul(float3x3(I.M1.x, I.M2.x, I.M3.x,
		I.M1.y, I.M2.y, I.M3.y,
		I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

	float height = sample_height(I.tcdh);
	height = height * (parallax.x) + (parallax.y);	//
	float2	new_tc = I.tcdh + height * normalize(eye);	//

	//	Output the result
	I.tcdh = new_tc;
}

#else	//	USE_PARALLAX

void UpdateTC(inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

void decode_bump(
	float4 Nu,
	float4 NuE,
	inout surface_bumped S
)
{
#ifdef USE_PBR

	// ------------------------------------------------
	// PBR texture layout
	//
	// _bump:
	// R = Height
	// G/B/A = Normal
	//
	// _bump#:
	// R = Metallic
	// G = Roughness
	// B = Reserved
	// A = AO
	// ------------------------------------------------

	// Existing X-Ray packing:
	// A = Normal X
	// B = Normal Y
	//
	// Reconstruct Z so _bump# is no longer needed
	// for normal correction.
	float2 normalXY = Nu.wz * 2.0f - 1.0f;

	S.normal.xy = normalXY;
	S.normal.z = sqrt(
		saturate(
			1.0f - dot(normalXY, normalXY)
		)
	);

	S.metallic = saturate(NuE.r);
	S.roughness = saturate(NuE.g);
	S.pbr_reserved = NuE.b;
	S.ao = saturate(NuE.a);

	// Temporary bridge for old lighting.
	// Later gloss disappears from the PBR lighting path.
	S.gloss = 1.0f - S.roughness;

	// Actual PBR height map.
	S.height = Nu.r;

#else

	// Original X-Ray path — untouched
	S.normal = Nu.wzy + (NuE.xyz - 1.0h);
	S.gloss = Nu.x * Nu.x;
	S.height = NuE.z;

	S.metallic = 0.0f;
	S.roughness = 1.0f - S.gloss;
	S.ao = 1.0f;
	S.pbr_reserved = 0.0f;

#endif
}

surface_bumped sload_i(p_bumped I)
{
	surface_bumped	S;

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 Nu =
		s_bump.Sample(smp_base, I.tcdh);

	float4 NuE =
		s_bumpX.Sample(smp_base, I.tcdh);

	S.base = tbase(I.tcdh);

	decode_bump(Nu, NuE, S);
	//S.height	= 0;

#ifdef USE_TDETAIL

	float4 detail = s_detail.Sample(smp_base, I.tcdbump);
	S.base.rgb = S.base.rgb * detail.rgb * 2;

#ifdef USE_TDETAIL_BUMP

	float4 NDetail =
		s_detailBump.Sample(smp_base, I.tcdbump);

	float4 NDetailX =
		s_detailBumpX.Sample(smp_base, I.tcdbump);

	S.gloss =
		S.gloss * NDetail.x * 2;

	S.normal +=
		NDetail.wzy +
		NDetailX.xyz -
		1.0h;

#else

	S.gloss =
		S.gloss * detail.w * 2;

#endif

#endif // USE_TDETAIL

	return S;
}

surface_bumped sload_i(p_bumped I, float2 pixeloffset)
{
	surface_bumped	S;

	// apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
	I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu = s_bump.Sample(smp_base, I.tcdh);		// IN:	normal.gloss
	float4 	NuE = s_bumpX.Sample(smp_base, I.tcdh);	// IN:	normal_error.height

	S.base = tbase(I.tcdh);				//	IN:  rgb.a
	decode_bump(Nu, NuE, S);
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
	I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail = s_detailBump.Sample(smp_base, I.tcdbump);
	float4 NDetailX = s_detailBumpX.Sample(smp_base, I.tcdbump);
	S.gloss = S.gloss * NDetail.x * 2;
	//S.normal			+= NDetail.wzy-.5;
	S.normal += NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)

	float4 detail = s_detail.Sample(smp_base, I.tcdbump);
	S.base.rgb = S.base.rgb * detail.rgb * 2;

	//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
	I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
	float4 detail = s_detail.Sample(smp_base, I.tcdbump);
	S.base.rgb = S.base.rgb * detail.rgb * 2;
	S.gloss = S.gloss * detail.w * 2;
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}


surface_bumped sload(p_bumped I)
{
	surface_bumped      S = sload_i(I);
#ifndef USE_PBR
	S.normal.z *= 0.5;
#endif

#ifdef	GBUFFER_OPTIMIZATION
	S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
	return              S;
}

surface_bumped sload(p_bumped I, float2 pixeloffset)
{
	surface_bumped      S = sload_i(I, pixeloffset);
#ifndef USE_PBR
	S.normal.z *= 0.5;
#endif
#ifdef	GBUFFER_OPTIMIZATION
	S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
	return              S;
}

#endif
#endif
