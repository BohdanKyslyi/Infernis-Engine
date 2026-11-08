#ifndef IMG_CORRECTIONS_H
#define IMG_CORRECTIONS_H
#include "common.h"
#include "anomaly_shaders.h"

// Monolith declares these two in the static_globals cbuffer of shared\common.h ( lines 52-53 ),
// so every shader there sees them without asking. This engine's shared\common.h has no such
// lines, and only this header needs them, so they are declared here. The engine already fills
// them: Blender_Recorder_StandartBinding registers pp_img_corrections and pp_img_cg from the
// r__exposure / r__gamma / r__saturation / r__color_grading console variables. Same situation
// as rain_params, which had to be declared in common_functions.h for the same reason.
uniform float4 pp_img_corrections; // x - exposure, y - gamma, z - saturation, w = 1
uniform float4 pp_img_cg;          // xyz - colour grading tint, w = 1

float3 img_corrections(float3 img)
{
	//exposure
	img.xyz *= pp_img_corrections.x;
	
	//color grading (thanks KD and Crytek and Cjayho)
	float fLum = dot(img.xyz, LUMINANCE_VECTOR);
	float3 cColor = lerp(0.0, pp_img_cg.xyz, saturate( fLum * 2.0 ) );
	cColor = lerp( cColor, 1.0, saturate( fLum - 0.5) * 2.0 );
	
	if (pp_img_cg.x > 0.0 || pp_img_cg.y > 0.0 || pp_img_cg.z > 0.0) 
	{
		img.xyz = saturate(lerp( img.xyz, cColor.xyz , saturate( fLum * 0.15 ) ));	
	}
	
	//saturation
	img.xyz = saturate(lerp(img.xyz, dot(img.xyz, LUMINANCE_VECTOR), (1.0 - pp_img_corrections.z))); 
	
	//gamma correction
	img.xyz = pow(img,(1.0/pp_img_corrections.y));
	
	//that's all :)
	return img.xyz;
}
#endif