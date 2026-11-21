/**
 * @ Version: SCREEN SPACE SHADERS - UPDATE 17
 * @ Description: Decals - VS
 * @ Modified time: 2025-04-05 15:36:55
 * @ Author: https://www.moddb.com/members/ascii1457
 * @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders
 */

#include "screenspace_mvectors.h"

#include "common.h"

v2p_TL main(v_TL I)
{
	v2p_TL O;

	O.HPos 	= mul(m_VP, I.P);
	O.Tex0 	= I.Tex0;
	O.Color = I.Color.bgra;

	// TAA Jitter
	O.HPos.xy = ssfx_taa_jitter(O.HPos);

	return O;
}