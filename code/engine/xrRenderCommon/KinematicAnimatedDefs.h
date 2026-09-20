#ifndef KINEMATIC_ANIMATED_DEFS_INCLUDED
#define KINEMATIC_ANIMATED_DEFS_INCLUDED

#pragma once

#include "xrEngine/SkeletonMotionDefs.h"
// consts
const u32 MAX_BLENDED = 16;
const u32 MAX_CHANNELS = 4;

const u32 MAX_BLENDED_POOL = (MAX_BLENDED * MAX_PARTS * MAX_CHANNELS);
// MotionID::slot and LL_MotionsSlotCount() are u16. Reserve the last value so
// loading external OMF libraries is limited by the ID representation, not 48 files.
const u32 MAX_ANIM_SLOT = 0xFFFF;
class CBlend;
typedef svector<CBlend*, MAX_BLENDED * MAX_CHANNELS> BlendSVec; //*MAX_CHANNELS
typedef BlendSVec::iterator BlendSVecIt;
typedef BlendSVec::const_iterator BlendSVecCIt;
//**********************************************************************************

#endif
