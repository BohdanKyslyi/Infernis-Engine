#include "stdafx.h"
#pragma hdrstop

#include "sh_atomic.h"
#include "ResourceManager.h"
#include "dxRenderDeviceRender.h"

SVS::~SVS() {
    DEV->_DeleteVS(this);
    _RELEASE(vs);
}

SPS::~SPS() {
    _RELEASE(ps);
    DEV->_DeletePS(this);
}

#if defined(USE_DX10) || defined(USE_DX11)
SGS::~SGS() {
    _RELEASE(gs);
    DEV->_DeleteGS(this);
}

#ifdef USE_DX11
SHS::~SHS() {
    _RELEASE(sh);
    DEV->_DeleteHS(this);
}
SDS::~SDS() {
    _RELEASE(sh);
    DEV->_DeleteDS(this);
}
SCS::~SCS() {
    _RELEASE(sh);
    DEV->_DeleteCS(this);
}
#endif

SInputSignature::SInputSignature(ID3DBlob* pBlob) {
    VERIFY(pBlob);
    signature = pBlob;
    signature->AddRef();
}

SInputSignature::~SInputSignature() {
    _RELEASE(signature);
    DEV->_DeleteInputSignature(this);
}
#endif // USE_DX10

SState::~SState() {
    _RELEASE(state);
    DEV->_DeleteState(this);
}

SDeclaration::~SDeclaration() {
    DEV->_DeleteDecl(this);
#if defined(USE_DX10) || defined(USE_DX11)
    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: C++17 Structured Bindings
    for (auto& [blob, layout] : vs_to_layout) {
        _RELEASE(layout);
    }
#else
    _RELEASE(dcl);
#endif
}