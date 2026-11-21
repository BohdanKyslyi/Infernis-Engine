// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
#ifdef IE_SSFX_ENABLED
#include "common.h"

#include "screenspace_mvectors.h"

//////////////////////////////////////////////////////////////////////////////////////////
// Vertex
v2p_TL main ( v_TL I )
{
	v2p_TL O;

	O.HPos = mul( m_VP, I.P );
	O.Tex0 = I.Tex0;
	O.Color = I.Color.bgra;	//	swizzle vertex colour

	// Test TAA
	O.HPos.xy = ssfx_taa_jitter(O.HPos);

 	return O;
}
#else
#include "common.h"

//////////////////////////////////////////////////////////////////////////////////////////
// Vertex
v2p_TL main ( v_TL I )
{
	v2p_TL O;

	O.HPos = mul( m_VP, I.P );
	O.Tex0 = I.Tex0;
	O.Color = I.Color.bgra;	//	swizzle vertex colour

 	return O;
}
#endif
