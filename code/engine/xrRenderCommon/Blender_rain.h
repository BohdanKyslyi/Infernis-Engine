#pragma once

#include "blenders\Blender.h"

class CBlender_rain_drops : public IBlender
{
public:
    virtual LPCSTR getComment()    { return "Custom Noir Rain Drops Blender"; }
    virtual BOOL canBeLMAPped()    { return FALSE; }
    virtual BOOL canBeDetailed()   { return FALSE; }
    virtual BOOL canBeStrictB2F()  { return TRUE; }

    virtual void Compile(CBlender_Compile& C) override;

    CBlender_rain_drops();
    virtual ~CBlender_rain_drops();
};