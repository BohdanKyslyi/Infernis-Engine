#pragma once

// Screen Space Shaders blenders.
// SSFX drives most of its passes from the engine rather than from a .s script, so every pass needs
// a blender that names the pixel shader and binds the buffers it reads. Ported from Monolith
// (Layers\xrRenderPC_R4\blender_blur.cpp, blender_bloom_build.cpp, blender_smaa.cpp,
// blender_dof.cpp). CBlender_blur and CBlender_dof at the end of the file are Monolith's own
// classes and keep their own names: the SSFX bloom and the SSFX depth of field depend on the
// passes they build. Monolith's CBlender_smaa is not part of SSFX and is not here.

class CBlender_ssfx_ssr : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_ssr"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_ssr();
    virtual ~CBlender_ssfx_ssr();
};

class CBlender_ssfx_volumetric_blur : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_volumetric_blur"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_volumetric_blur();
    virtual ~CBlender_ssfx_volumetric_blur();
};

class CBlender_ssfx_ao : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_ao"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_ao();
    virtual ~CBlender_ssfx_ao();
};

class CBlender_ssfx_sss : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_sss"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_sss();
    virtual ~CBlender_ssfx_sss();
};

class CBlender_ssfx_sss_ext : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_sss_ext"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_sss_ext();
    virtual ~CBlender_ssfx_sss_ext();
};

class CBlender_ssfx_rain : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_rain"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_rain();
    virtual ~CBlender_ssfx_rain();
};

class CBlender_ssfx_water_blur : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_water"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_water_blur();
    virtual ~CBlender_ssfx_water_blur();
};

class CBlender_ssfx_motion_blur : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_motion_blur"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_motion_blur();
    virtual ~CBlender_ssfx_motion_blur();
};

class CBlender_ssfx_fog_scattering : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_fog_scattering"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_fog_scattering();
    virtual ~CBlender_ssfx_fog_scattering();
};

class CBlender_ssfx_taa : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_taa"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_taa();
    virtual ~CBlender_ssfx_taa();
};

class CBlender_ssfx_bloom_build : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_bloom"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_bloom_build();
    virtual ~CBlender_ssfx_bloom_build();
};

class CBlender_ssfx_bloom_lens : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_bloom_lens"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_bloom_lens();
    virtual ~CBlender_ssfx_bloom_lens();
};

class CBlender_ssfx_bloom_downsample : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_bloom_downsample"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_bloom_downsample();
    virtual ~CBlender_ssfx_bloom_downsample();
};

class CBlender_ssfx_bloom_upsample : public IBlender {
public:
    virtual LPCSTR getComment() { return "ssfx_bloom_upsample"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_ssfx_bloom_upsample();
    virtual ~CBlender_ssfx_bloom_upsample();
};

// Monolith Layers\xrRenderPC_R4\blender_blur.h
class CBlender_blur : public IBlender {
public:
    virtual LPCSTR getComment() { return "Blur generation"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_blur();
    virtual ~CBlender_blur();
};

// Monolith Layers\xrRenderPC_R4\blender_dof.h
class CBlender_dof : public IBlender {
public:
    virtual LPCSTR getComment() { return "DoF"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_dof();
    virtual ~CBlender_dof();
};
