/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 14.4
 * @ Description: 2 Layers fog ( Distance + Height )
 * @ Modified time: 2025-04-04 01:37:30
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

uniform float4 ssfx_fog; // Height, Density, Sun Color, -

float SSFX_HEIGHT_FOG(float3 P, float World_Py, inout float3 color)
{
	// Get Sun dir
	float3 Sun = saturate(dot(normalize(Ldynamic_dir), -normalize(P)));

	// Apply sun color
	Sun = lerp(fog_color, Ldynamic_color.rgb, Sun);

	// Distance Fog ( Default Anomaly Fog )
	float fog = saturate(length(P) * fog_params.w + fog_params.x);

	// Height Fog
	float fogheight = smoothstep(ssfx_fog.x, -ssfx_fog.x, World_Py);

	// Add the height fog to the distance fog
	float fogresult = saturate(fog + fogheight * (fog * ssfx_fog.y));

	// Blend factor to mix sun color and fog color. Adjust intensity to.
	float FogBlend = fogheight * ssfx_fog.z;

	// Final fog color
	float3 FOG_COLOR = lerp(fog_color, Sun, FogBlend);

	// Apply fog to color
	color = lerp(color, FOG_COLOR, fogresult);

	// Return distance fog.
	return fog;
}

float SSFX_FOGGING(float Fog, float World_Py)
{
	// Height fog
	float fog_height = smoothstep(ssfx_fog.x, -ssfx_fog.x, World_Py);

	// Add height fog to default distance fog.
	float fog_extra = saturate(Fog + fog_height * (Fog * ssfx_fog.y));
	
	return 1.0f - fog_extra;
}

float SSFX_CALC_FOG(float3 P)
{
	float3 WorldP = mul(m_inv_V, float4(P.xyz, 1));
	float distance = length(P.xyz);
	float fog = saturate(distance * fog_params.w + fog_params.x); // Vanilla fog
	float fogheight = smoothstep(ssfx_fog.x, -ssfx_fog.x, WorldP.y); // Height fog
	
	return saturate(fog + fogheight * (fog * ssfx_fog.y));
}