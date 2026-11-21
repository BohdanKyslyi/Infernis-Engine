#include "stdafx.h"
#include "dxUIRender.h"

#include "dxUIShader.h"
#include "blenders/Blender.h"
#include "Blender_Screen_SET.h"
#include "ResourceManager.h"
#include "dxRenderDeviceRender.h"
#if defined(USE_DX10) || defined(USE_DX11)
#include "../xrRenderDX10/StateManager/dx10ShaderResourceStateCache.h"
#endif

dxUIRender UIRenderImpl;

void dxUIRender::CreateUIGeom() {
    hGeom_TL.create(FVF::F_TL, RCache.Vertex.Buffer(), 0);
    hGeom_LIT.create(FVF::F_LIT, RCache.Vertex.Buffer(), 0);
}

void dxUIRender::DestroyUIGeom() {
    ReleaseWeatherPreview();
    hGeom_TL = NULL;
    hGeom_LIT = NULL;
}

void dxUIRender::SetShader(IUIShader& shader) {
    dxUIShader* pShader = (dxUIShader*)&shader;
    VERIFY(&pShader);
    VERIFY(pShader->hShader);
    RCache.set_Shader(pShader->hShader);
}

void dxUIRender::SetAlphaRef(int aref) {
    // CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAREF,aref));
    RCache.set_AlphaRef(aref);
}
/*
void dxUIRender::StartTriList(u32 iMaxVerts)
{
        VERIFY(PrimitiveType==ptNone);
        m_PointType = pttLIT;
        m_iMaxVerts = iMaxVerts;
        start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
        pv			= start_pv;
        PrimitiveType = ptTriList;
}

void dxUIRender::FlushTriList()
{
        VERIFY(PrimitiveType==ptTriList);
        VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

        std::ptrdiff_t p_cnt		= (pv-start_pv)/3;
        RCache.Vertex.Unlock		(u32(pv-start_pv),hGeom_fan.stride());
        RCache.set_Geometry			(hGeom_fan);
        if (p_cnt!=0)RCache.Render	(D3DPT_TRIANGLELIST,vOffset,u32(p_cnt));

        PrimitiveType = ptNone;
}

void dxUIRender::StartTriFan(u32 iMaxVerts)
{
        VERIFY(PrimitiveType==ptNone);
        m_iMaxVerts = iMaxVerts;
        start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
        pv			= start_pv;
        PrimitiveType = ptTriFan;
        m_PointType	= pttLIT;

}

void dxUIRender::FlushTriFan()
{
        VERIFY(PrimitiveType==ptTriFan);
        VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

        std::ptrdiff_t p_cnt		= pv-start_pv;
        RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
        RCache.set_Geometry	 		(hGeom_fan);
        if (p_cnt>2) RCache.Render	(D3DPT_TRIANGLEFAN,vOffset,u32(p_cnt-2));

        PrimitiveType = ptNone;
}

void dxUIRender::StartTriStrip(u32 iMaxVerts)
{
        VERIFY(PrimitiveType==ptNone);
        m_iMaxVerts = iMaxVerts;
        start_pv	= (FVF::TL*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
        pv			= start_pv;
        PrimitiveType = ptTriStrip;
}

void dxUIRender::FlushTriStrip()
{
}


void dxUIRender::StartLineStrip(u32 iMaxVerts)
{
        VERIFY(PrimitiveType==ptNone);
        m_iMaxVerts = iMaxVerts;
        start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
        pv			= start_pv;
        PrimitiveType = ptLineStrip;
        m_PointType = pttLIT;
}

void dxUIRender::FlushLineStrip()
{
        VERIFY(PrimitiveType==ptLineStrip);
        VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

        std::ptrdiff_t p_cnt		= pv-start_pv;
        RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
        RCache.set_Geometry	 		(hGeom_fan);
        if (p_cnt>1) RCache.Render	(D3DPT_LINESTRIP,vOffset,u32(p_cnt-1));

        PrimitiveType = ptNone;
}

void dxUIRender::StartLineList(u32 iMaxVerts)
{
        VERIFY(PrimitiveType==ptNone);
        m_iMaxVerts = iMaxVerts;
        start_pv	= (FVF::LIT*)RCache.Vertex.Lock	(m_iMaxVerts,hGeom_fan.stride(),vOffset);
        pv			= start_pv;
        PrimitiveType = ptLineList;
}

void dxUIRender::FlushLineList()
{
        VERIFY(PrimitiveType==ptLineList);
        VERIFY(u32(pv-start_pv)<=m_iMaxVerts);

        std::ptrdiff_t p_cnt		= pv-start_pv;
        RCache.Vertex.Unlock		(u32(p_cnt),hGeom_fan.stride());
        RCache.set_Geometry	 		(hGeom_fan);
        if (p_cnt>1) RCache.Render	(D3DPT_LINELIST,vOffset,u32(p_cnt)/2);

        PrimitiveType = ptNone;
}
*/
void dxUIRender::SetScissor(Irect* rect) {
#if (RENDER == R_R3) || (RENDER == R_R4)
    RCache.set_Scissor(rect);
    StateManager.OverrideScissoring(rect ? true : false, TRUE);
#else  //	(RENDER == R_R3) || (RENDER == R_R4)
    RCache.set_Scissor(rect);
#endif //	(RENDER == R_R3) || (RENDER == R_R4)
}

void dxUIRender::GetActiveTextureResolution(Fvector2& res) {
    CTexture* T = RCache.get_ActiveTexture(0);
    res.set(float(T->get_Width()), float(T->get_Height()));
}

LPCSTR dxUIRender::UpdateShaderName(LPCSTR tex_name, LPCSTR sh_name) {
    string_path buff;
    u32 v_dev = CAP_VERSION(HW.Caps.raster_major, HW.Caps.raster_minor);
    u32 v_need = CAP_VERSION(2, 0);
    // strstr(Core.Params,"-ps_movie") &&
    if ((v_dev >= v_need) && FS.exist(buff, "$game_textures$", tex_name, ".ogm"))
        return "hud\\movie";
    else
        return sh_name;
}
/*
void dxUIRender::PushPoint(float x, float y, u32 c, float u, float v)
{
        VERIFY(m_PointType==pttNone);
        pv->set(x, y, 0.0f, c, u, v);
        ++pv;
}
*/
/*
void dxUIRender::PushPoint(int x, int y, u32 c, float u, float v)
{
        VERIFY(m_PointType==pttNone);
        pv->set(x, y, 0, c, u, v);
        ++pv;
}
*/

void dxUIRender::PushPoint(float x, float y, float z, u32 C, float u, float v) {
    //.	VERIFY(m_PointType==pttLIT);
    switch (m_PointType) {
    case pttLIT:
        LIT_pv->set(x, y, z, C, u, v);
        ++LIT_pv;
        break;
    case pttTL:
        TL_pv->set(x, y, C, u, v);
        ++TL_pv;
        break;
    }
}

void dxUIRender::StartPrimitive(u32 iMaxVerts, ePrimitiveType primType, ePointType pointType) {
    VERIFY(PrimitiveType == ptNone);
    VERIFY(m_PointType == ptNone);
    //.	R_ASSERT(pointType==pttLIT);

    m_iMaxVerts = iMaxVerts;
    PrimitiveType = primType;
    m_PointType = pointType;

    switch (m_PointType) {
    case pttLIT:
        LIT_start_pv = (FVF::LIT*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_LIT.stride(), vOffset);
        LIT_pv = LIT_start_pv;
        break;
    case pttTL:
        TL_start_pv = (FVF::TL*)RCache.Vertex.Lock(m_iMaxVerts, hGeom_TL.stride(), vOffset);
        TL_pv = TL_start_pv;
        break;
    }
}

void dxUIRender::FlushPrimitive() {
    u32 primCount = 0;
    _D3DPRIMITIVETYPE d3dPrimType = D3DPT_FORCE_DWORD;
    std::ptrdiff_t p_cnt = 0;

    switch (m_PointType) {
    case pttLIT:
        p_cnt = LIT_pv - LIT_start_pv;
        VERIFY(u32(p_cnt) <= m_iMaxVerts);

        RCache.Vertex.Unlock(u32(p_cnt), hGeom_LIT.stride());
        RCache.set_Geometry(hGeom_LIT);
        break;
    case pttTL:
        p_cnt = TL_pv - TL_start_pv;
        VERIFY(u32(p_cnt) <= m_iMaxVerts);

        RCache.Vertex.Unlock(u32(p_cnt), hGeom_TL.stride());
        RCache.set_Geometry(hGeom_TL);
        break;
    default:
        NODEFAULT;
    }

    //	Update data for primitive type
    switch (PrimitiveType) {
    case ptTriStrip:
        primCount = (u32)(p_cnt - 2);
        d3dPrimType = D3DPT_TRIANGLESTRIP;
        break;
    case ptTriList:
        primCount = (u32)(p_cnt / 3);
        d3dPrimType = D3DPT_TRIANGLELIST;
        break;
    case ptLineStrip:
        primCount = (u32)(p_cnt - 1);
        d3dPrimType = D3DPT_LINESTRIP;
        break;
    case ptLineList:
        primCount = (u32)(p_cnt / 2);
        d3dPrimType = D3DPT_LINELIST;
        break;
    default:
        NODEFAULT;
    }

    if (primCount > 0)
        RCache.Render(d3dPrimType, vOffset, primCount);

    PrimitiveType = ptNone;
    m_PointType = pttNone;
}

void dxUIRender::CacheSetXformWorld(const Fmatrix& M) { RCache.set_xform_world(M); }

void dxUIRender::CacheSetCullMode(CullMode m) { RCache.set_CullMode(CULL_NONE + m); }


// Reuse the completed scene once per editor draw; do not render the level twice.
// CRT participates in the resource manager's device-reset lifecycle.
bool dxUIRender::DrawWeatherPreview(const Frect& destination) {
    if (destination.width() <= 0.f || destination.height() <= 0.f)
        return false;
    u32 width = 0, height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
#if defined(USE_DX10) || defined(USE_DX11)
    ID3DRenderTargetView* view = RCache.get_RT();
    if (!view) return false;
    ID3DResource* resource = nullptr;
    view->GetResource(&resource);
    ID3DTexture2D* source = nullptr;
    const HRESULT query = resource->QueryInterface(__uuidof(ID3DTexture2D), (void**)&source);
    _RELEASE(resource);
    if (FAILED(query) || !source) return false;
    D3D_TEXTURE2D_DESC desc;
    source->GetDesc(&desc);
    width = desc.Width; height = desc.Height;
    switch (desc.Format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: format = D3DFMT_A8R8G8B8; break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: format = D3DFMT_A16B16G16R16F; break;
    default: _RELEASE(source); return false;
    }
#else
    IDirect3DSurface9* source = nullptr;
    if (FAILED(HW.pDevice->GetRenderTarget(0, &source)) || !source) return false;
    D3DSURFACE_DESC desc;
    if (FAILED(source->GetDesc(&desc))) { _RELEASE(source); return false; }
    width = desc.Width; height = desc.Height; format = desc.Format;
#endif
    if (m_weatherScene && (m_weatherScene->dwWidth != width ||
        m_weatherScene->dwHeight != height || m_weatherScene->fmt != format))
        ReleaseWeatherPreview();
    if (!m_weatherScene)
        m_weatherScene.create("$user$weather_editor_scene", width, height, format);
    if (!m_weatherScene || !m_weatherScene->valid()) {
        _RELEASE(source);
        return false;
    }

    // The copy destination may still be a sampler from the previous UI pass.
    if (!m_weatherEmptyTextures) {
        STextureList empty;
        m_weatherEmptyTextures = DEV->_CreateTextureList(empty);
    }
    RCache.set_Textures(m_weatherEmptyTextures);
#if defined(USE_DX10) || defined(USE_DX11)
    SRVSManager.Apply();
    if (desc.SampleDesc.Count > 1)
        HW.pContext->ResolveSubresource(m_weatherScene->pSurface, 0, source, 0, desc.Format);
    else
        HW.pContext->CopyResource(m_weatherScene->pSurface, source);
    _RELEASE(source);
#else
    const HRESULT copy = HW.pDevice->StretchRect(source, nullptr,
        m_weatherScene->pRT, nullptr, D3DTEXF_NONE);
    _RELEASE(source);
    if (FAILED(copy)) return false;
#endif
    if (!m_weatherShader) {
        // Opaque copy: scene alpha is not a UI opacity channel.
        CBlender_Screen_SET blender;
        m_weatherShader.create(&blender, "$user$weather_editor_copy", "$user$weather_editor_scene");
    }
    RCache.set_Shader(m_weatherShader);
    RCache.set_CullMode(CULL_NONE);

    Frect image = destination;
    const float scale = std::min(image.width() / float(width), image.height() / float(height));
    const float w = width * scale, h = height * scale;
    image.x1 += (image.width() - w) * .5f;
    image.y1 += (image.height() - h) * .5f;
    image.x2 = image.x1 + w; image.y2 = image.y1 + h;
    // Clear the fullscreen scene visually, then display the entire camera frame
    // inside the editor. Black bars preserve aspect ratio at any resolution.
    StartPrimitive(12, ptTriList, pttTL);
    const auto quad = [this](const Frect& r, u32 color) {
#if defined(USE_DX10) || defined(USE_DX11)
        const float offset = 0.f;
#else
        const float offset = -.5f;
#endif
        const float l = r.x1 + offset, t = r.y1 + offset;
        const float rr = r.x2 + offset, b = r.y2 + offset;
        PushPoint(l,t,0.f,color,0.f,0.f); PushPoint(rr,t,0.f,color,1.f,0.f);
        PushPoint(rr,b,0.f,color,1.f,1.f); PushPoint(l,t,0.f,color,0.f,0.f);
        PushPoint(rr,b,0.f,color,1.f,1.f); PushPoint(l,b,0.f,color,0.f,1.f);
    };
    Frect screen; screen.set(0.f,0.f,float(width),float(height));
    quad(screen, color_rgba(0,0,0,255));
    quad(image, color_rgba(255,255,255,255));
    FlushPrimitive();
    // Unbind our sample so next frame's copy never writes a bound texture.
    RCache.set_Textures(m_weatherEmptyTextures);
#if defined(USE_DX10) || defined(USE_DX11)
    SRVSManager.Apply();
#endif
    return true;
}

void dxUIRender::ReleaseWeatherPreview() {
    m_weatherShader.destroy();
    m_weatherScene.destroy();
}


// Optional versioned entry point. Never extend/reorder IUIRender's shared vtable.
// nullptr releases the temporary preview resources.
extern "C" __declspec(dllexport) BOOL __cdecl xrWeatherEditorPreviewV1(const Frect* destination) {
    if (!destination) {
        UIRenderImpl.ReleaseWeatherPreview();
        return TRUE;
    }
    return UIRenderImpl.DrawWeatherPreview(*destination) ? TRUE : FALSE;
}
