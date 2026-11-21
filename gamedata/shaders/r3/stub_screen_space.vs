#include "common.h"

// screen_res is declared in common_functions.h, which common.h already pulls in. The donor
// declares it here because Anomaly's own common.h does not.

//////////////////////////////////////////////////////////////////////////////////////////
// Vertex
p_screen main(v2p_screen I)
{
	p_screen O;
	// Transform to screen space (in d3d9 it was done automatically)
	O.hpos.x 	=  (I.HPos.x * screen_res.z * 2 - 1);
	O.hpos.y 	= -(I.HPos.y * screen_res.w * 2 - 1);
	O.hpos.zw 	=	I.HPos.zw;
	
    O.tc0 		= 	I.tc0;
  
    return O; 
}