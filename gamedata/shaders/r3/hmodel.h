// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
// Screen Space Shaders. Two versions of hmodel(), chosen by the pseudo-PBR switch
// ( SSFX_ENHANCED_SHADERS, check_screenspace_ES.h ):
//   - on:  the SSFX folder's own hmodel.h - Enhanced Shaders 1.10 as SSFX patches it - word for
//          word but for the sky reflection flora loses, a setting of the SSS window now
//          ( ES_FLORA_SKY_CUT, pbr_settings.h - 0.75 in the donor ).
//          It takes the pixel's albedo and gloss ( float4 alb_gloss ), reads only the two ambient
//          cubes env_s0 / env_s1, and returns the hemisphere light in linear light. It calls
//          calc_albedo(), Amb_BRDF() and the rest of pbr_brdf.h, which lmodel.h includes; both
//          callers ( combine_1.ps, screenspace_common.h ) include lmodel.h first.
//   - off: Monolith's vanilla hmodel.h, byte for byte, as before.
// Infernis's own hmodel.h fits neither: it declares m_v2w and sky_s0 / sky_s1, while the donor's
// screenspace_common.h calls hmodel() with Monolith's seven arguments and the rest of the donor's
// tree expects m_inv_V.
//
// This does not touch PBR. combine_1.ps only reaches hmodel() in its legacy branch - the #else of
// the PBR path, which goes through ie_pbr_ambient_ibl() and never calls this function. Infernis's
// PBR ambient is therefore exactly as it was, with the module on or off.
//
// Knock-on effect, recorded so it is not rediscovered: Infernis's ssao.ps reads m_v2w, which was
// declared in Infernis's hmodel.h and is gone with it. That is why the legacy SSAO group -
// ssao*.ps and depth_downs.ps - comes from Monolith vanilla too. See audit_origin.py.
#ifndef        HMODEL_H
#define HMODEL_H

// Screen Space Shaders: the pseudo-PBR switch, read here for the same reason as in lmodel.h.
#include "check_screenspace_ES.h"

#ifdef SSFX_ENHANCED_SHADERS
#define CUBE_MIPS 6 //mipmaps for ambient shading and specular

#include "pbr_cubemap_check.h"
//gamma correction is set up to be semi gamma correct
#endif

TextureCube		env_s0;
TextureCube		env_s1;

uniform float4		env_color;        // color.w  = lerp factor
float4 hmodel_stuff;  //x - hemi vibrance // y - hemi contrast // z - wet surface factor

#ifdef SSFX_ENHANCED_SHADERS
void hmodel
(
	out float3 hdiffuse, out float3 hspecular, 
	float m, float h, float4 alb_gloss, float3 Pnt, float3 normal
)
{
	// [ SSS Test ]. Overwrite terrain material
	bool m_terrain = abs(m - 0.95) <= 0.04f;
	bool m_flora = abs(m - 0.15) <= 0.04f;
	if (m_terrain)
		m = 0;
	
	//PBR style
	float3 albedo = calc_albedo(alb_gloss, m);
	float3 specular = calc_specular(alb_gloss, m);
	float rough = calc_rough(alb_gloss, m);
	//calc_rain(albedo, specular, rough, alb_gloss, m, h);
	//calc_foliage(albedo, specular, rough, float4(0.05,0.05,0.05,0.05), m);
	
	float roughCube = rough;
	//float roughCube = sqrt(rough); //cubemap mipmaps (brdf too?)

	//float RoughMip = roughCube * CUBE_MIPS;
	float RoughMip = CUBE_MIPS - ((1 - roughCube) * CUBE_MIPS);
	
	//normal vector
	normal = normalize(normal);
	float3	nw = mul(m_inv_V, normal);
	//nw = normalize(nw);
	
	//view vector
	Pnt = normalize(Pnt);
	float3	v2Pnt = mul(m_inv_V, Pnt);
	//v2Pnt = normalize(v2Pnt);

	//normal remap
	float3 nwRemap = nw;
	float3 vnormabs   = abs(nwRemap);
	float  vnormmax   = max(vnormabs.x, max(vnormabs.y, vnormabs.z));
	nwRemap      /= vnormmax;
    if (nwRemap.y < 0.999) nwRemap.y = nwRemap.y*2-1;     // fake remapping 
	
	//reflection vector
	float3	vreflect= reflect(v2Pnt, nw );

	//reflect remap
	float3 vreflectRemap = vreflect;
	float3 vreflectabs = abs(vreflectRemap);
	float vreflectmax = max(vreflectabs.x, max(vreflectabs.y, vreflectabs.z));
	vreflectRemap	  /= vreflectmax;
	if (vreflectRemap.y < 0.999) vreflectRemap.y = vreflectRemap.y*2-1;	//fake remapping 
	
	//normalize
	nwRemap = normalize(nwRemap);
	vreflectRemap = normalize(vreflectRemap);
	
	//DICE reflection vector roughness
	vreflectRemap = getSpecularDominantDir(nwRemap, vreflectRemap, rough);
	
	//Valve style ambient cube to prevent seams
	const float Epsilon = 0.001;
	float3 nSquared = nw * nw;
	
	float3 e0d = 0;
	e0d += nSquared.x * (env_s0.SampleLevel(smp_base, float3(nwRemap.x, Epsilon, Epsilon), CUBE_MIPS).rgb);
	e0d += nSquared.y * (env_s0.SampleLevel(smp_base, float3(Epsilon, nwRemap.y, Epsilon), CUBE_MIPS).rgb); 
	e0d += nSquared.z * (env_s0.SampleLevel(smp_base, float3(Epsilon, Epsilon, nwRemap.z), CUBE_MIPS).rgb); 
	//e0d = LinearTosRGB(e0d);
	
	//e0d = env_s0.SampleLevel(smp_base, nwRemap, CUBE_MIPS);
	
	float3 e1d = 0;
	e1d += nSquared.x * (env_s1.SampleLevel(smp_base, float3(nwRemap.x, Epsilon, Epsilon), CUBE_MIPS).rgb);
	e1d += nSquared.y * (env_s1.SampleLevel(smp_base, float3(Epsilon, nwRemap.y, Epsilon), CUBE_MIPS).rgb); 
	e1d += nSquared.z * (env_s1.SampleLevel(smp_base, float3(Epsilon, Epsilon, nwRemap.z), CUBE_MIPS).rgb);
	//e1d = LinearTosRGB(e1d);
	
	//e1d = env_s1.SampleLevel(smp_base, nwRemap, CUBE_MIPS);
	
	//specular color
	float3	e0s		= env_s0.SampleLevel( smp_base, vreflectRemap, RoughMip );
	float3	e1s		= env_s1.SampleLevel( smp_base, vreflectRemap, RoughMip );

	//lerp
	float3 env_d = lerp(e0d, e1d, env_color.w);
	float3 env_s = lerp(e0s, e1s, env_color.w);

	// hscale - something like diffuse reflection
	float	hscale = h;	//. * (.5h + .5h*nw.y);
	float	hspec	= .5h + .5h * dot( vreflect, v2Pnt);
	
	//TODO - make hscale normal mapped
	float4	light	= float4(hscale, hscale, hscale, hscale);
	//float4	light	= s_material.SampleLevel( smp_material, float3( hscale, hspec, m ), 0 ).xxxy;
	
	//tint color
	//float3 env_col = 1.0; 
	float3 env_col = env_color.rgb; 
	//float3 env_col = fog_color.rgb * 2.0; 
	
	env_d *= env_col; 		 
	env_s *= env_col;
	
	//lightmap ambient
	env_d *= light.xxx; 		
	env_s *= light.www; 	
	
	//ambient color
	float3 amb_col = L_ambient.rgb;	
	env_d += amb_col; 			
	env_s += amb_col; //*(env_s/env_d);
		
	env_d = SRGBToLinear(env_d); 
	env_s = SRGBToLinear(env_s);	//gamma correct
	
	env_s *= 1.0f - m_flora * ES_FLORA_SKY_CUT; // this port: 0.75f in the donor, a setting ( pbr_settings.h )

	hdiffuse = Amb_BRDF(rough, albedo, specular, env_d, env_s, -v2Pnt, nw ).rgb;
	hspecular = 0; //do not use hspec at all
}
#else
void hmodel
(
	out float3 hdiffuse, out float3 hspecular, 
	float m, float h, float s, float3 Pnt, float3 normal
)
{
	normal = normalize(normal);	
	
// hscale - something like diffuse reflection
	float3	nw		= mul( m_inv_V, normal );
	float	hscale	= h;	//. *        (.5h + .5h*nw.y);

#ifdef         USE_GAMMA_22
			hscale	= (hscale*hscale);        // make it more linear
#endif

// reflection vector
	float3	v2PntL	= normalize( Pnt );
	float3	v2Pnt	= mul( m_inv_V, v2PntL );
	float3	vreflect= reflect( v2Pnt, nw );
	float	hspec	= .5h + .5h * dot( vreflect, v2Pnt );


// material	// sample material
	float4	light	= s_material.SampleLevel( smp_material, float3( hscale, hspec, m ), 0 ).xxxy;


// diffuse color
	float3	e0d		= env_s0.SampleLevel( smp_rtlinear, nw, 0 );
	float3	e1d		= env_s1.SampleLevel( smp_rtlinear, nw, 0 );
	float3	env_d	= env_color.xyz * lerp( e0d, e1d, env_color.w );
			env_d	*=env_d;
	
	hdiffuse= env_d * light.xyz + L_ambient.rgb;


// specular color
	float3 vreflectabs    = abs(vreflect);
    float  vreflectmax    = max(vreflectabs.x, max(vreflectabs.y, vreflectabs.z));
           vreflect      /= vreflectmax;
       if (vreflect.y < 0.999)    
            vreflect.y= vreflect.y*2-1;     // fake remapping 
	float3	e0s		= env_s0.SampleLevel( smp_rtlinear, vreflect, 0 );
	float3	e1s		= env_s1.SampleLevel( smp_rtlinear, vreflect, 0 );
	float3	env_s	= env_color.xyz * lerp( e0s, e1s, env_color.w);
			env_s	*=env_s;
	float luminance = 1-dot( light.rgb, float3(LUMINANCE_VECTOR) );

	hspecular	= env_s*light.w*(s+(rain_params.x*0.25))*luminance*15*(rain_params.x*2);
}
#endif
#endif
#else
#ifndef        HMODEL_H
#define HMODEL_H

#include "common.h"

//uniform samplerCUBE         env_s0                ;
//uniform samplerCUBE         env_s1                ;
//uniform samplerCUBE         sky_s0                ;
//uniform samplerCUBE         sky_s1                ;

TextureCube		env_s0;
TextureCube		env_s1;
TextureCube		sky_s0;
TextureCube		sky_s1;

uniform float4	env_color;        // color.w  = lerp factor
uniform float3x4	m_v2w;

void hmodel
(
	out float3 hdiffuse, out float3 hspecular, 
	float m, float h, float s, float3 Pnt, float3 normal
)
{
        // hscale - something like diffuse reflection
	float3	nw		= mul( m_v2w, normal );
	float	hscale	= h;	//. *        (.5h + .5h*nw.y);

#ifdef         USE_GAMMA_22
			hscale	= (hscale*hscale);        // make it more linear
#endif

	// reflection vector
	float3	v2PntL	= normalize( Pnt );
	float3	v2Pnt	= mul( m_v2w, v2PntL );
	float3	vreflect= reflect( v2Pnt, nw );
	float	hspec	= .5h + .5h * dot( vreflect, v2Pnt );

	// material	// sample material
	//float4	light	= tex3D( s_material, float3(hscale, hspec, m) );
//	float4	light	= s_material.Sample( smp_material, float3( hscale, hspec, m ) ).xxxy;
	float4	light	= s_material.SampleLevel( smp_material, float3( hscale, hspec, m ), 0 ).xxxy;
//	float4	light	= float4(1,1,1,1);

	// diffuse color
//	float3	e0d		= texCUBE( env_s0, nw );
//	float3	e1d		= texCUBE( env_s1, nw );
//	float3	e0d		= env_s0.Sample( smp_rtlinear, nw );
//	float3	e1d		= env_s1.Sample( smp_rtlinear, nw );
	float3	e0d		= env_s0.SampleLevel( smp_rtlinear, nw, 0 );
	float3	e1d		= env_s1.SampleLevel( smp_rtlinear, nw, 0 );
	float3	env_d	= env_color.xyz * lerp( e0d, e1d, env_color.w );
			env_d	*=env_d;	// contrast
			hdiffuse= env_d * light.xyz + L_ambient.rgb;

	// specular color
	vreflect.y      = vreflect.y*2-1;	// fake remapping
//	float3	e0s		= texCUBE( env_s0, vreflect );
//	float3	e1s		= texCUBE( env_s1, vreflect );
//	float3	e0s		= env_s0.Sample( smp_rtlinear, vreflect );
//	float3	e1s		= env_s1.Sample( smp_rtlinear, vreflect );
	float3	e0s		= env_s0.SampleLevel( smp_rtlinear, vreflect, 0 );
	float3	e1s		= env_s1.SampleLevel( smp_rtlinear, vreflect, 0 );
	float3	env_s	= env_color.xyz * lerp( e0s, e1s, env_color.w);
			env_s	*=env_s;	// contrast
		hspecular	= env_s*light.w*s;                //*h*m*s        ;        //env_s        *light.w         * s;
}

/*
void         hmodel_table        (out float3 hdiffuse, out float3 hspecular, float m, float h, float s, float3 point, float3 normal)
{
        // hscale - something like diffuse reflection
        float         hscale         = h;

        // reflection vector
        float3         v2point        = normalize        (Pnt);
        float3        vreflect= reflect         (v2point,normal);
        float         hspec         = .5h+.5h*dot        (vreflect,v2point);

        // material
          float4         light        = tex3D                (s_material, float3(hscale, hspec, m) );                // sample material

        // diffuse color
        float3         env_d         = texCUBE         (env_s0,normal);

        // specular color
        float3         env_s          = texCUBE         (env_s0,vreflect);

        //
        hdiffuse        = env_d        *light.xyz         + L_ambient.rgb        ;
        hspecular        = env_s        *light.w         * s                ;
}
*/
#endif
#endif
