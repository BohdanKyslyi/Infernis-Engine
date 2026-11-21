#pragma once
#include "Flashlight.h"

class CUIArtefactDetectorSimple;

class CSimpleDetector : public CFlashlight {
    typedef CFlashlight inherited;

public:
    CSimpleDetector();
    virtual ~CSimpleDetector();

protected:
    //.	virtual void 	UpdateZones					();
    virtual void UpdateAf();
    virtual void CreateUI();
    CUIArtefactDetectorSimple& ui();
};
