// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
#define	v_in	v_static_color
#else
#define	v_in	v_static
#endif

#include "screenspace_mvectors.h"

v2p_flat main ( v_in I )
{
	I.Nh			= unpack_D3DCOLOR(I.Nh);
	I.T				= unpack_D3DCOLOR(I.T);
	I.B				= unpack_D3DCOLOR(I.B);

	// Eye-space pos/normal
	v2p_flat 		O;
	float4	Pp 	= mul( m_WVP, I.P );
	O.hpos 		= Pp;
	O.N 		= mul( (float3x3)m_WV, unpack_bx2(I.Nh) );
	float3	Pe	= mul( m_WV, I.P );

	float2	tc 	= unpack_tc_base( I.tc, I.T.w, I.B.w);	// copy tc
	O.tcdh		= float4( tc.xyyy );
	O.position	= float4( Pe, I.Nh.w );

	// Values to calc motion vectors
	O.vel_curr = O.hpos;
	O.vel_prev = mul(m_wvp_prev, I.P);

	// TAA Jitter
	O.hpos.xy = ssfx_taa_jitter(O.hpos);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float 	s	= I.color.w	;							// (r,g,b,dir-occlusion)
	O.tcdh.w	= s;
#endif

#ifdef	USE_TDETAIL
	O.tcdbump	= O.tcdh * dt_params;					// dt tc
#endif

#ifdef	USE_LM_HEMI
	O.lmh 		= unpack_tc_lmap( I.lmh );
#endif

	return	O;
}
FXVS;
#else
#include "common.h"

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
#define	v_in	v_static_color
#else
#define	v_in	v_static
#endif


v2p_flat main ( v_in I )
{
	I.Nh			= unpack_D3DCOLOR(I.Nh);
	I.T				= unpack_D3DCOLOR(I.T);
	I.B				= unpack_D3DCOLOR(I.B);

	// Eye-space pos/normal
	v2p_flat 		O;
	float4	Pp 	= mul( m_WVP, I.P );
	O.hpos 		= Pp;
	O.N 		= mul( (float3x3)m_WV, unpack_bx2(I.Nh) );
	float3	Pe	= mul( m_WV, I.P );

	float2	tc 	= unpack_tc_base( I.tc, I.T.w, I.B.w);	// copy tc
	O.tcdh		= float4( tc.xyyy );
	O.position	= float4( Pe, I.Nh.w );

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float 	s	= I.color.w	;							// (r,g,b,dir-occlusion)
	O.tcdh.w	= s;
#endif

#ifdef	USE_TDETAIL
	O.tcdbump	= O.tcdh * dt_params;					// dt tc
#endif

#ifdef	USE_LM_HEMI
	O.lmh 		= unpack_tc_lmap( I.lmh );
#endif

	return	O;
}
FXVS;
#endif
