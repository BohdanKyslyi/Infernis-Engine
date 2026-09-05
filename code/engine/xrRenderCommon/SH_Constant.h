#ifndef SH_CONSTANT_H
#define SH_CONSTANT_H
#pragma once

#include "xrEngine/WaveForm.h"

class IReader;
class IWriter;

class ECORE_API CConstant : public xr_resource_named {
public:
    enum { modeProgrammable = 0, modeWaveForm };

public:
    Fcolor const_float{};
    u32 const_dword{ 0 };

    u32 dwFrame{ 0 };
    u32 dwMode{ 0 };
    
    WaveForm _R{};
    WaveForm _G{};
    WaveForm _B{};
    WaveForm _A{};

    // Більше ніякого std::memset(this, 0, ...) !
    CConstant() = default;

    inline void set_float(float r, float g, float b, float a) noexcept {
        const_float.set(r, g, b, a);
        const_dword = const_float.get();
    }
    
    inline void set_float(const Fcolor& c) noexcept {
        const_float.set(c);
        const_dword = const_float.get();
    }
    
    inline void set_dword(u32 c) noexcept {
        const_float.set(c);
        const_dword = c;
    }
    
    void Calculate();
    
    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: const-коректність та noexcept
    [[nodiscard]] inline BOOL Similar(const CConstant& C) const noexcept {
        if (dwMode != C.dwMode) return FALSE;
        if (!_R.Similar(C._R)) return FALSE;
        if (!_G.Similar(C._G)) return FALSE;
        if (!_B.Similar(C._B)) return FALSE;
        if (!_A.Similar(C._A)) return FALSE;
        return TRUE;
    }
    
    void Load(IReader* fs);
    void Save(IWriter* fs);
};

typedef resptr_core<CConstant, resptr_base<CConstant>> ref_constant_obsolette;

#endif // SH_CONSTANT_H