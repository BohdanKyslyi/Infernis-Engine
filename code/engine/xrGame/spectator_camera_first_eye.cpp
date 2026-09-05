#include "stdafx.h"
#include "spectator_camera_first_eye.h"
#include "xr_level_controller.h"
#include "../xrEngine/xr_object.h"
#include <algorithm> 

CSpectrCameraFirstEye::CSpectrCameraFirstEye(float const& fTimeDelta, CObject* p, u32 flags)
    : inherited(p, flags), m_fTimeDelta(fTimeDelta) 
{
}

void CSpectrCameraFirstEye::Move(int cmd, float val, float factor) 
{
    if (bClampPitch) {
        while (pitch < lim_pitch[0]) pitch += PI_MUL_2;
        while (pitch > lim_pitch[1]) pitch -= PI_MUL_2;
    }
    
    switch (cmd) {
    case kDOWN:
        pitch -= (val != 0.0f) ? val : (rot_speed.y * m_fTimeDelta / factor);
        break;
    case kUP:
        pitch += (val != 0.0f) ? val : (rot_speed.y * m_fTimeDelta / factor);
        break;
    case kLEFT:
        yaw -= (val != 0.0f) ? val : (rot_speed.x * m_fTimeDelta / factor);
        break;
    case kRIGHT:
        yaw += (val != 0.0f) ? val : (rot_speed.x * m_fTimeDelta / factor);
        break;
    default:
        break;
    }
    
    if (bClampYaw) {
        yaw = std::clamp(yaw, lim_yaw[0], lim_yaw[1]);
    }
    if (bClampPitch) {
        pitch = std::clamp(pitch, lim_pitch[0], lim_pitch[1]);
    }
}