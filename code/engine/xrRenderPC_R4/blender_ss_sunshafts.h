#pragma once

// OGSE screen-space sun shafts.
// Ported from Monolith src\Layers\xrRenderPC_R4\blender_ss_sunshafts.h, unchanged apart from this
// tree's brace style. Five elements: mask, three blur passes, and the pass that blends the result
// back over the scene.

class CBlender_sunshafts : public IBlender {
public:
    virtual LPCSTR getComment() { return "OGSE: sunshafts"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_sunshafts();
    virtual ~CBlender_sunshafts();
};
