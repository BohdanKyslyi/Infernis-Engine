#ifndef DetailModelH
#define DetailModelH
#pragma once

#include "IRenderDetailModel.h"

class ECORE_API CDetail : public IRender_DetailModel {
public:
    ~CDetail() override;

    void Load(IReader* S);
    void Optimize();
    
    void Unload();

    void transfer(Fmatrix& mXform, fvfVertexOut* vDest, u32 C, u16* iDest, u32 iOffset) override;
    void transfer(Fmatrix& mXform, fvfVertexOut* vDest, u32 C, u16* iDest, u32 iOffset, float du, float dv) override;
};

#endif // DetailModelH