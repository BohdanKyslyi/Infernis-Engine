#pragma once

#include "../xrcore/ftimer.h"
#include "CameraFirstEye.h"

class CSpectrCameraFirstEye : public CCameraFirstEye {
private:
    using inherited = CCameraFirstEye;
    float const& m_fTimeDelta;

public:
    CSpectrCameraFirstEye(float const& fTimeDelta, CObject* p, u32 flags = 0);
    ~CSpectrCameraFirstEye() override = default;
    CSpectrCameraFirstEye(const CSpectrCameraFirstEye&) = delete;
    CSpectrCameraFirstEye& operator=(const CSpectrCameraFirstEye&) = delete;

    virtual void Move(int cmd, float val = 0.0f, float factor = 1.0f) override;
};