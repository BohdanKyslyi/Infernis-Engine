#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"

#ifndef _EDITOR
#include "xrEngine/render.h"
#endif

#include "xrEngine/tntQAVI.h"
#include "xrEngine/xrTheora_Surface.h"
#include "dxRenderDeviceRender.h"

#define PRIORITY_HIGH 12
#define PRIORITY_NORMAL 8
#define PRIORITY_LOW 4

void resptrcode_texture::create(LPCSTR _name) { _set(DEV->_CreateTexture(_name)); }

CTexture::CTexture() {
    // Вся пам'ять вже безпечно ініціалізована нулями в заголовковому файлі!
    bind = fastdelegate::FastDelegate1<u32>(this, &CTexture::apply_load);
}

CTexture::~CTexture() {
    Unload();
    DEV->_DeleteTexture(this);
}

void CTexture::surface_set(ID3DBaseTexture* surf) {
    if (surf) surf->AddRef();
    _RELEASE(pSurface);
    pSurface = surf;
}

ID3DBaseTexture* CTexture::surface_get() {
    if (pSurface) pSurface->AddRef();
    return pSurface;
}

void CTexture::PostLoad() {
    if (pTheora)
        bind = fastdelegate::FastDelegate1<u32>(this, &CTexture::apply_theora);
    else if (pAVI)
        bind = fastdelegate::FastDelegate1<u32>(this, &CTexture::apply_avi);
    else if (!seqDATA.empty())
        bind = fastdelegate::FastDelegate1<u32>(this, &CTexture::apply_seq);
    else
        bind = fastdelegate::FastDelegate1<u32>(this, &CTexture::apply_normal);
}

void __stdcall CTexture::apply_load(u32 dwStage) {
    if (!flags.bLoaded) Load();
    else PostLoad();
    bind(dwStage);
}

void __stdcall CTexture::apply_theora(u32 dwStage) {
    if (pTheora->Update(m_play_time != 0xFFFFFFFF ? m_play_time : RDEVICE.dwTimeContinual)) {
        R_ASSERT(D3DRTYPE_TEXTURE == pSurface->GetType());
        ID3DTexture2D* T2D = static_cast<ID3DTexture2D*>(pSurface);
        D3DLOCKED_RECT R;
        RECT rect{ 0, 0, static_cast<LONG>(pTheora->Width(true)), static_cast<LONG>(pTheora->Height(true)) };

        u32 _w = pTheora->Width(false);

        R_CHK(T2D->LockRect(0, &R, &rect, 0));
        R_ASSERT(R.Pitch == static_cast<int>(pTheora->Width(false) * 4));
        int _pos = 0;
        pTheora->DecompressFrame(static_cast<u32*>(R.pBits), _w - rect.right, _pos);
        VERIFY(static_cast<u32>(_pos) == rect.bottom * _w);
        R_CHK(T2D->UnlockRect(0));
    }
    CHK_DX(HW.pDevice->SetTexture(dwStage, pSurface));
}

void __stdcall CTexture::apply_avi(u32 dwStage) {
    if (pAVI->NeedUpdate()) {
        R_ASSERT(D3DRTYPE_TEXTURE == pSurface->GetType());
        ID3DTexture2D* T2D = static_cast<ID3DTexture2D*>(pSurface);

        D3DLOCKED_RECT R;
        R_CHK(T2D->LockRect(0, &R, nullptr, 0));
        R_ASSERT(R.Pitch == static_cast<int>(pAVI->m_dwWidth * 4));
        
        BYTE* ptr;
        pAVI->GetFrame(&ptr);
        std::memcpy(R.pBits, ptr, pAVI->m_dwWidth * pAVI->m_dwHeight * 4);
        R_CHK(T2D->UnlockRect(0));
    }
    CHK_DX(HW.pDevice->SetTexture(dwStage, pSurface));
}

void __stdcall CTexture::apply_seq(u32 dwStage) {
    u32 frame = RDEVICE.dwTimeContinual / seqMSPF;
    u32 frame_data = static_cast<u32>(seqDATA.size());
    if (flags.seqCycles) {
        u32 frame_id = frame % (frame_data * 2);
        if (frame_id >= frame_data)
            frame_id = (frame_data - 1) - (frame_id % frame_data);
        pSurface = seqDATA[frame_id];
    } else {
        u32 frame_id = frame % frame_data;
        pSurface = seqDATA[frame_id];
    }
    CHK_DX(HW.pDevice->SetTexture(dwStage, pSurface));
}

void __stdcall CTexture::apply_normal(u32 dwStage) { 
    CHK_DX(HW.pDevice->SetTexture(dwStage, pSurface)); 
}

void CTexture::Preload() {
    m_bumpmap = DEV->m_textures_description.GetBumpName(cName);
    m_material = DEV->m_textures_description.GetMaterial(cName);
}

void CTexture::Load() {
    flags.bLoaded = true;
    desc_cache = nullptr;
    if (pSurface) return;

    flags.bUser = false;
    flags.MemoryUsage = 0;
    if (0 == stricmp(*cName, "$null")) return;
    if (0 != strstr(*cName, "$user$")) {
        flags.bUser = true;
        return;
    }

    Preload();
    string_path fn;
    if (FS.exist(fn, "$game_textures$", *cName, ".ogm")) {
        pTheora = xr_new<CTheoraSurface>();
        m_play_time = 0xFFFFFFFF;

        if (!pTheora->Load(fn)) {
            xr_delete(pTheora);
            FATAL("Can't open video stream");
        } else {
            flags.MemoryUsage = pTheora->Width(true) * pTheora->Height(true) * 4;
            BOOL bstop_at_end = (0 != strstr(cName.c_str(), "intro\\")) || (0 != strstr(cName.c_str(), "outro\\"));
            pTheora->Play(!bstop_at_end, RDEVICE.dwTimeContinual);

            ID3DTexture2D* pTexture = nullptr;
            u32 _w = pTheora->Width(false);
            u32 _h = pTheora->Height(false);

            HRESULT hrr = HW.pDevice->CreateTexture(_w, _h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, nullptr);
            pSurface = pTexture;
            if (FAILED(hrr)) {
                FATAL("Invalid video stream");
                R_CHK(hrr);
                xr_delete(pTheora);
                pSurface = nullptr;
            }
        }
    } else if (FS.exist(fn, "$game_textures$", *cName, ".avi")) {
        pAVI = xr_new<CAviPlayerCustom>();

        if (!pAVI->Load(fn)) {
            xr_delete(pAVI);
            FATAL("Can't open video stream");
        } else {
            flags.MemoryUsage = pAVI->m_dwWidth * pAVI->m_dwHeight * 4;

            ID3DTexture2D* pTexture = nullptr;
            HRESULT hrr = HW.pDevice->CreateTexture(pAVI->m_dwWidth, pAVI->m_dwHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, nullptr);
            pSurface = pTexture;
            if (FAILED(hrr)) {
                FATAL("Invalid video stream");
                R_CHK(hrr);
                xr_delete(pAVI);
                pSurface = nullptr;
            }
        }
    } else if (FS.exist(fn, "$game_textures$", *cName, ".seq")) {
        string256 buffer;
        IReader* _fs = FS.r_open(fn);

        flags.seqCycles = FALSE;
        _fs->r_string(buffer, sizeof(buffer));
        if (0 == stricmp(buffer, "cycled")) {
            flags.seqCycles = TRUE;
            _fs->r_string(buffer, sizeof(buffer));
        }
        u32 fps = atoi(buffer);
        seqMSPF = 1000 / fps;

        while (!_fs->eof()) {
            _fs->r_string(buffer, sizeof(buffer));
            _Trim(buffer);
            if (buffer[0]) {
                u32 mem = 0;
                ID3DBaseTexture* pTex = ::RImplementation.texture_load(buffer, mem);
                if (pTex) {
                    seqDATA.push_back(pTex);
                    flags.MemoryUsage += mem;
                }
            }
        }
        pSurface = nullptr;
        FS.r_close(_fs);
    } else {
        u32 mem = 0;
        pSurface = ::RImplementation.texture_load(*cName, mem);
        if (pSurface) {
            flags.MemoryUsage = mem;
        }
    }
    PostLoad();
}

void CTexture::Unload() {
#ifdef DEBUG
    string_path msg_buff;
    xr_sprintf(msg_buff, sizeof(msg_buff), "* Unloading texture [%s] pSurface RefCount=", cName.c_str());
#endif 

    flags.bLoaded = FALSE;
    if (!seqDATA.empty()) {
        // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Range-based for замість індексного циклу
        for (auto* tex : seqDATA) {
            _RELEASE(tex);
        }
        seqDATA.clear();
        pSurface = nullptr;
    }
    flags.MemoryUsage = 0;

#ifdef DEBUG
    _SHOW_REF(msg_buff, pSurface);
#endif 

    _RELEASE(pSurface);

    xr_delete(pAVI);
    xr_delete(pTheora);

    bind = fastdelegate::FastDelegate1<u32>(this, &CTexture::apply_load);
}

void CTexture::desc_update() {
    desc_cache = pSurface;
    if (pSurface && (D3DRTYPE_TEXTURE == pSurface->GetType())) {
        ID3DTexture2D* T = static_cast<ID3DTexture2D*>(pSurface);
        R_CHK(T->GetLevelDesc(0, &desc));
    }
}

void CTexture::video_Play(BOOL looped, u32 _time) {
    if (pTheora)
        pTheora->Play(looped, (_time != 0xFFFFFFFF) ? (m_play_time = _time) : RDEVICE.dwTimeContinual);
}

void CTexture::video_Pause(BOOL state) {
    if (pTheora) pTheora->Pause(state);
}

void CTexture::video_Stop() {
    if (pTheora) pTheora->Stop();
}

BOOL CTexture::video_IsPlaying() { return (pTheora) ? pTheora->IsPlaying() : FALSE; }