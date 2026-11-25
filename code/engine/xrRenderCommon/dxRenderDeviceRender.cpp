#include "stdafx.h"
#include "dxRenderDeviceRender.h"

#include "ResourceManager.h"

#if RENDER == R_R4
// Item 9 stage A ( multithreading ): the GPU time of a frame for the frame measurement
// ( "cpu_frame_stats 1", xrEngine\device.cpp ). Two timestamps a frame - right after Begin and
// right before Present - inside a disjoint query. Six sets in a ring, each read back five frames
// later without waiting ( D3D11_ASYNC_GETDATA_DONOTFLUSH ), so the measurement never holds a frame
// up; a set the GPU has not finished by then is dropped. DX11 only: the other renderers keep their
// code. Our own design, the donor has none.
extern ENGINE_API int ps_cpu_frame_stats;
ENGINE_API void frame_stats_add_gpu(float ms);
// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the render parts of a frame
// ( r4_frame_parts.h ): at each boundary between two parts its CPU time and a GPU timestamp; the CPU
// parts are handed over at the frame's end, the GPU ones when the frame's set is read back. The video
// memory every 30 frames ( r4_vram_query.cpp, Windows 10+ ).
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини рендеру кадру
// ( r4_frame_parts.h ): на кожній межі між двома частинами - її час процесора й мітка часу
// відеокарти; частини процесора передаються в кінці кадру, частини відеокарти - коли набір кадру
// зчитується. Відеопам'ять - кожні 30 кадрів ( r4_vram_query.cpp, Windows 10+ ).
#include "r4_frame_parts.h"
ENGINE_API void frame_stats_add_render_parts(const float* cpu_ms, const char* const* names,
                                             u32 count);
ENGINE_API void frame_stats_add_gpu_parts(const float* gpu_ms, u32 count, float frame_ms);
ENGINE_API void frame_stats_set_vram(u64 used, u64 budget, u64 nonlocal);
bool r4_vram_query(IUnknown* device, unsigned long long* used, unsigned long long* budget,
                   unsigned long long* nonlocal);

namespace {
const u32 gpu_timer_sets = 6;
// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the boundaries a frame can have
// ( the main view 15, the lens view 2 )
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - скільки меж може мати кадр
// ( основний вид 15, вид лінзи 2 )
const u32 gpu_part_marks = 32;

struct gpu_frame_timer {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* stamp_begin = nullptr;
    ID3D11Query* stamp_end = nullptr;
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the boundaries of the render parts
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - межі частин рендеру
    ID3D11Query* stamp_mark[gpu_part_marks] = {};
    u8 mark_part[gpu_part_marks] = {}; // the part of the work before the boundary
    u32 marks = 0;                     // the boundaries of the views that reached their end
    bool issued = false;
};
gpu_frame_timer gpu_timers[gpu_timer_sets];
u32 gpu_timer_frame = 0;
bool gpu_timer_open = false; // Begin started the set of this frame

// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the render parts of this frame on
// the CPU side and the hand-over of a frame's parts ( the comment at the top )
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини рендеру цього кадру з боку
// процесора й передавання частин кадру ( коментар угорі )
struct render_part_state {
    u64 qpc[gpu_part_marks] = {}; // the CPU time of each boundary of this frame
    u32 pending = 0;              // the boundaries written this frame
    u32 view_frame = 0;           // Device.dwFrame of the open view
    bool open = false;            // a view is open
    bool lens = false;            // the open view is the 3D scope's lens
};
render_part_state render_parts;
const u8 render_part_view_start = 0xff; // a boundary that starts a view: no part ends at it
const char* const render_part_names[R4P_COUNT] = {
    "visibility", "GPU sync wait", "scene",  "grass", "SSS",         "sun",        "emissive",
    "lights",     "AO+IL",         "combine/reflections + water", "transparent", "post",
    "scope lens"};

double render_parts_ms_per_tick() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return 1000.0 / double(freq.QuadPart);
}

// a boundary of the open view in this frame's set
void render_parts_boundary(u8 part) {
    render_part_state& r = render_parts;
    if (r.pending >= gpu_part_marks)
        return;
    gpu_frame_timer& t = gpu_timers[gpu_timer_frame % gpu_timer_sets];
    const u32 i = r.pending++;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    r.qpc[i] = u64(now.QuadPart);
    t.mark_part[i] = part;
    if (t.stamp_mark[i])
        HW.pContext->End(t.stamp_mark[i]);
}

// this frame's CPU parts ( gpu_timer_end, before Present )
void render_parts_hand_cpu(const gpu_frame_timer& t) {
    if (!t.marks)
        return;
    float ms[R4P_COUNT] = {};
    const double k = render_parts_ms_per_tick();
    for (u32 i = 1; i < t.marks; ++i)
        if (t.mark_part[i] < R4P_COUNT)
            ms[t.mark_part[i]] += float(double(render_parts.qpc[i] - render_parts.qpc[i - 1]) * k);
    frame_stats_add_render_parts(ms, render_part_names, R4P_COUNT);
}

// an earlier frame's GPU parts, its set read back ( gpu_timer_end ); a stamp not ready drops them
void render_parts_hand_gpu(const gpu_frame_timer& t, UINT64 frequency, UINT64 frame_ticks) {
    if (!t.marks)
        return;
    UINT64 stamp[gpu_part_marks] = {};
    for (u32 i = 0; i < t.marks; ++i)
        if (!t.stamp_mark[i] ||
            HW.pContext->GetData(t.stamp_mark[i], &stamp[i], sizeof(UINT64),
                                 D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            return;
    float ms[R4P_COUNT] = {};
    for (u32 i = 1; i < t.marks; ++i) {
        if (t.mark_part[i] >= R4P_COUNT)
            continue;
        if (stamp[i] < stamp[i - 1])
            return;
        ms[t.mark_part[i]] += float(double(stamp[i] - stamp[i - 1]) * 1000.0 / double(frequency));
    }
    frame_stats_add_gpu_parts(ms, R4P_COUNT,
                              float(double(frame_ticks) * 1000.0 / double(frequency)));
}

// the video memory ( every 30 frames while measured )
void render_vram_read() {
    unsigned long long used = 0, budget = 0, nonlocal = 0;
    if (r4_vram_query(HW.pDevice, &used, &budget, &nonlocal))
        frame_stats_set_vram(used, budget, nonlocal);
}

void gpu_timer_release(ID3D11Query*& q) {
    if (q) {
        q->Release();
        q = nullptr;
    }
}

void gpu_timers_release() {
    for (gpu_frame_timer& t : gpu_timers) {
        gpu_timer_release(t.disjoint);
        gpu_timer_release(t.stamp_begin);
        gpu_timer_release(t.stamp_end);
        // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the boundaries' queries and the
        // open view
        // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - запити меж і відкритий
        // вид
        for (ID3D11Query*& q : t.stamp_mark)
            gpu_timer_release(q);
        t.marks = 0;
        t.issued = false;
    }
    gpu_timer_open = false;
    render_parts.open = false;
}

bool gpu_timers_create() {
    if (gpu_timers[0].disjoint)
        return true;
    D3D11_QUERY_DESC disjoint_desc = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    D3D11_QUERY_DESC stamp_desc = { D3D11_QUERY_TIMESTAMP, 0 };
    for (gpu_frame_timer& t : gpu_timers)
        if (FAILED(HW.pDevice->CreateQuery(&disjoint_desc, &t.disjoint)) ||
            FAILED(HW.pDevice->CreateQuery(&stamp_desc, &t.stamp_begin)) ||
            FAILED(HW.pDevice->CreateQuery(&stamp_desc, &t.stamp_end))) {
            gpu_timers_release();
            return false;
        }
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the boundaries' queries; a frame
    // without them keeps its total
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - запити меж; кадр без них зберігає
    // свій загальний час
    for (gpu_frame_timer& t : gpu_timers)
        for (ID3D11Query*& q : t.stamp_mark)
            if (FAILED(HW.pDevice->CreateQuery(&stamp_desc, &q)))
                q = nullptr;
    return true;
}

void gpu_timer_begin() {
    gpu_timer_open = false;
    if (!ps_cpu_frame_stats) {
        for (gpu_frame_timer& t : gpu_timers)
            t.issued = false;
        return;
    }
    if (!gpu_timers_create())
        return;
    gpu_frame_timer& t = gpu_timers[gpu_timer_frame % gpu_timer_sets];
    HW.pContext->Begin(t.disjoint);
    HW.pContext->End(t.stamp_begin);
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - this frame's render parts start
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - починаються частини рендеру цього
    // кадру
    t.marks = 0;
    render_parts.pending = 0;
    render_parts.open = false;
    gpu_timer_open = true;
}

void gpu_timer_end() {
    if (!gpu_timer_open)
        return;
    gpu_timer_open = false;
    gpu_frame_timer& t = gpu_timers[gpu_timer_frame % gpu_timer_sets];
    HW.pContext->End(t.stamp_end);
    HW.pContext->End(t.disjoint);
    t.issued = true;
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - this frame's CPU parts; the video
    // memory every 30 frames
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини процесора цього кадру;
    // відеопам'ять кожні 30 кадрів
    render_parts.open = false;
    render_parts_hand_cpu(t);
    ++gpu_timer_frame;
    if (gpu_timer_frame % 30 == 0)
        render_vram_read();

    // the oldest set, the one the next frame starts again
    gpu_frame_timer& old = gpu_timers[gpu_timer_frame % gpu_timer_sets];
    if (!old.issued)
        return;
    old.issued = false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
    UINT64 t0 = 0, t1 = 0;
    if (HW.pContext->GetData(old.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
        HW.pContext->GetData(old.stamp_begin, &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
        HW.pContext->GetData(old.stamp_end, &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        return;
    if (dj.Disjoint || !dj.Frequency || t1 < t0)
        return;
    frame_stats_add_gpu(float(double(t1 - t0) * 1000.0 / double(dj.Frequency)));
    // Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - that frame's GPU parts
    // Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини відеокарти того кадру
    render_parts_hand_gpu(old, dj.Frequency, t1 - t0);
}
} // namespace

// Edited by Ivan Danko, 2026-11-14: item 9 "full measurement" - the marks of the render parts
// ( r4_frame_parts.h ): a view opens in CRender::Calculate and counts at the end of CRender::Render;
// a view that ended early is dropped by the next one
// Змінено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - позначки частин рендеру
// ( r4_frame_parts.h ): вид відкривається в CRender::Calculate і зараховується в кінці
// CRender::Render; вид, що завершився раніше, відкидає наступний
void r4_frame_parts_pass_begin() {
    render_part_state& r = render_parts;
    r.open = false;
    if (!gpu_timer_open)
        return;
    r.pending = gpu_timers[gpu_timer_frame % gpu_timer_sets].marks;
    r.lens = Device.scopeLensPass;
    r.view_frame = Device.dwFrame;
    r.open = true;
    render_parts_boundary(render_part_view_start);
}

void r4_frame_parts_mark(u32 part) {
    if (render_parts.open && !render_parts.lens && gpu_timer_open)
        render_parts_boundary(u8(part));
}

void r4_frame_parts_pass_end() {
    render_part_state& r = render_parts;
    if (!r.open)
        return;
    r.open = false;
    if (!gpu_timer_open || r.view_frame != Device.dwFrame)
        return;
    if (r.lens)
        render_parts_boundary(u8(R4P_LENS));
    gpu_timers[gpu_timer_frame % gpu_timer_sets].marks = r.pending;
}
#endif // RENDER == R_R4

dxRenderDeviceRender::dxRenderDeviceRender() : Resources(0) { ; }

void dxRenderDeviceRender::Copy(IRenderDeviceRender& _in) { *this = *(dxRenderDeviceRender*)&_in; }

void dxRenderDeviceRender::setGamma(float fGamma) { m_Gamma.Gamma(fGamma); }

void dxRenderDeviceRender::setBrightness(float fGamma) { m_Gamma.Brightness(fGamma); }

void dxRenderDeviceRender::setContrast(float fGamma) { m_Gamma.Contrast(fGamma); }

void dxRenderDeviceRender::updateGamma() { m_Gamma.Update(); }

void dxRenderDeviceRender::OnDeviceDestroy(BOOL bKeepTextures) {
    m_WireShader.destroy();
    m_SelectionShader.destroy();

    Resources->OnDeviceDestroy(bKeepTextures);
    RCache.OnDeviceDestroy();
}

void dxRenderDeviceRender::ValidateHW() { HW.Validate(); }

void dxRenderDeviceRender::DestroyHW() {
#if RENDER == R_R4
    gpu_timers_release(); // item 9 stage A: the frame measurement's GPU queries
#endif
    xr_delete(Resources);
    HW.DestroyDevice();
}

void dxRenderDeviceRender::Reset(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2,
                                 float& fHeight_2) {
#ifdef DEBUG
    _SHOW_REF("*ref -CRenderDevice::ResetTotal: DeviceREF:", HW.pDevice);
#endif // DEBUG

    Resources->reset_begin();
    Memory.mem_compact();
    HW.Reset(hWnd);

#if defined(USE_DX10) || defined(USE_DX11)
    dwWidth = HW.m_ChainDesc.BufferDesc.Width;
    dwHeight = HW.m_ChainDesc.BufferDesc.Height;
#else //	USE_DX10
    dwWidth = HW.DevPP.BackBufferWidth;
    dwHeight = HW.DevPP.BackBufferHeight;
#endif //	USE_DX10

    fWidth_2 = float(dwWidth / 2);
    fHeight_2 = float(dwHeight / 2);
    Resources->reset_end();

#ifdef DEBUG
    _SHOW_REF("*ref +CRenderDevice::ResetTotal: DeviceREF:", HW.pDevice);
#endif // DEBUG
}

void dxRenderDeviceRender::SetupStates() {
    HW.Caps.Update();

#if defined(USE_DX10) || defined(USE_DX11)
//	TODO: DX10: Implement Resetting of render states into default mode
// VERIFY(!"dxRenderDeviceRender::SetupStates not implemented.");
#else //	USE_DX10
    for (u32 i = 0; i < HW.Caps.raster.dwStages; i++) {
        float fBias = -.5f;
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, 4));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, *((LPDWORD)(&fBias))));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
        CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
    }
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_DITHERENABLE, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_COLORVERTEX, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_ZENABLE, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_LOCALVIEWER, TRUE));

    CHK_DX(HW.pDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR1));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE));

    if (psDeviceFlags.test(rsWireframe)) {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME));
    } else {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
    }

    // ******************** Fog parameters
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGCOLOR, 0));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE));
    if (HW.Caps.bTableFog) {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
    } else {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE));
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR));
    }

#endif //	USE_DX10
}

void dxRenderDeviceRender::OnDeviceCreate(LPCSTR shName) {
    // Signal everyone - device created
    RCache.OnDeviceCreate();
    m_Gamma.Update();
    Resources->OnDeviceCreate(shName);
    ::Render->create();
    Device.Statistic->OnDeviceCreate();

    m_WireShader.create("editor\\wire");
    m_SelectionShader.create("editor\\selection");

    DUImpl.OnDeviceCreate();
}

void dxRenderDeviceRender::Create(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2,
                                  float& fHeight_2, bool move_window) {
    HW.CreateDevice(hWnd, move_window);
#if defined(USE_DX10) || defined(USE_DX11)
    dwWidth = HW.m_ChainDesc.BufferDesc.Width;
    dwHeight = HW.m_ChainDesc.BufferDesc.Height;
#else //	USE_DX10
    dwWidth = HW.DevPP.BackBufferWidth;
    dwHeight = HW.DevPP.BackBufferHeight;
#endif //	USE_DX10
    fWidth_2 = float(dwWidth / 2);
    fHeight_2 = float(dwHeight / 2);
    Resources = xr_new<CResourceManager>();
}

void dxRenderDeviceRender::SetupGPU(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF) {
    HW.Caps.bForceGPU_SW = bForceGPU_SW;
    HW.Caps.bForceGPU_NonPure = bForceGPU_NonPure;
    HW.Caps.bForceGPU_REF = bForceGPU_REF;
}

void dxRenderDeviceRender::overdrawBegin() {
#if defined(USE_DX10) || defined(USE_DX11)
    //	TODO: DX10: Implement overdrawBegin
    VERIFY(!"dxRenderDeviceRender::overdrawBegin not implemented.");
#else  //	USE_DX10
    // Turn stenciling
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILENABLE, TRUE));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILREF, 0));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILMASK, 0x00000000));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILWRITEMASK, 0xffffffff));

    // Increment the stencil buffer for each pixel drawn
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT));

    if (1 == HW.Caps.SceneMode) {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP));
    } // Overdraw
    else {
        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_INCRSAT));
    } // ZB access
#endif //	USE_DX10
}

void dxRenderDeviceRender::overdrawEnd() {
#if defined(USE_DX10) || defined(USE_DX11)
    //	TODO: DX10: Implement overdrawEnd
    VERIFY(!"dxRenderDeviceRender::overdrawBegin not implemented.");
#else  //	USE_DX10
    // Set up the stencil states
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL));
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILMASK, 0xff));

    // Set the background to black
    CHK_DX(HW.pDevice->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0), 0, 0));

    // Draw a rectangle wherever the count equal I
    RCache.OnFrameEnd();
    CHK_DX(HW.pDevice->SetFVF(FVF::F_TL));

    // Render gradients
    for (int I = 0; I < 12; I++) {
        u32 _c = I * 256 / 13;
        u32 c = D3DCOLOR_XRGB(_c, _c, _c);

        FVF::TL pv[4];
        pv[0].set(float(0), float(Device.dwHeight), c, 0, 0);
        pv[1].set(float(0), float(0), c, 0, 0);
        pv[2].set(float(Device.dwWidth), float(Device.dwHeight), c, 0, 0);
        pv[3].set(float(Device.dwWidth), float(0), c, 0, 0);

        CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILREF, I));
        CHK_DX(HW.pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, pv, sizeof(FVF::TL)));
    }
    CHK_DX(HW.pDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE));
#endif //	USE_DX10
}

void dxRenderDeviceRender::DeferredLoad(BOOL E) { Resources->DeferredLoad(E); }

void dxRenderDeviceRender::ResourcesDeferredUpload() { Resources->DeferredUpload(); }

void dxRenderDeviceRender::ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps,
                                                   u32& c_lmaps) {
    if (Resources)
        Resources->_GetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);
}

void dxRenderDeviceRender::ResourcesStoreNecessaryTextures() {
    dxRenderDeviceRender::Instance().Resources->StoreNecessaryTextures();
}

void dxRenderDeviceRender::ResourcesDumpMemoryUsage() {
    dxRenderDeviceRender::Instance().Resources->_DumpMemoryUsage();
}

dxRenderDeviceRender::DeviceState dxRenderDeviceRender::GetDeviceState() {
    HW.Validate();
#if defined(USE_DX10) || defined(USE_DX11)
//	TODO: DX10: Implement GetDeviceState
//	TODO: DX10: Implement DXGI_PRESENT_TEST testing
// VERIFY(!"dxRenderDeviceRender::overdrawBegin not implemented.");
#else  //	USE_DX10
    HRESULT _hr = HW.pDevice->TestCooperativeLevel();
    if (FAILED(_hr)) {
        // If the device was lost, do not render until we get it back
        if (D3DERR_DEVICELOST == _hr)
            return dsLost;

        // Check if the device is ready to be reset
        if (D3DERR_DEVICENOTRESET == _hr)
            return dsNeedReset;
    }
#endif //	USE_DX10

    return dsOK;
}

BOOL dxRenderDeviceRender::GetForceGPU_REF() { return HW.Caps.bForceGPU_REF; }

u32 dxRenderDeviceRender::GetCacheStatPolys() { return RCache.stat.polys; }

void dxRenderDeviceRender::Begin() {
#if !defined(USE_DX10) && !defined(USE_DX11)
    CHK_DX(HW.pDevice->BeginScene());
#endif //	USE_DX10
#if RENDER == R_R4
    gpu_timer_begin(); // item 9 stage A: frame measurement
#endif
    RCache.OnFrameBegin();
    RCache.set_CullMode(CULL_CW);
    RCache.set_CullMode(CULL_CCW);
    if (HW.Caps.SceneMode)
        overdrawBegin();
}

void dxRenderDeviceRender::Clear() {
#if defined(USE_DX10) || defined(USE_DX11)
    HW.pContext->ClearDepthStencilView(RCache.get_ZB(), D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.0f,
                                       0);

    if (psDeviceFlags.test(rsClearBB)) {
        FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        HW.pContext->ClearRenderTargetView(RCache.get_RT(), ColorRGBA);
    }
#else  //	USE_DX10
    CHK_DX(HW.pDevice->Clear(0, 0,
                             D3DCLEAR_ZBUFFER |
                                 (psDeviceFlags.test(rsClearBB) ? D3DCLEAR_TARGET : 0) |
                                 (HW.Caps.bStencil ? D3DCLEAR_STENCIL : 0),
                             D3DCOLOR_XRGB(0, 0, 0), 1, 0));
#endif //	USE_DX10
}

void DoAsyncScreenshot();

void dxRenderDeviceRender::End() {
    VERIFY(HW.pDevice);

    if (HW.Caps.SceneMode)
        overdrawEnd();

    RCache.OnFrameEnd();

    DoAsyncScreenshot();

#if defined(USE_DX10) || defined(USE_DX11)
#if RENDER == R_R4
    gpu_timer_end(); // item 9 stage A: frame measurement
    // item 9 stage C1: "rs_v_sync" works on DX11 - one vertical blank a frame while it is on
    HW.m_pSwapChain->Present(psDeviceFlags.test(rsVSync) ? 1 : 0, 0);
#else
    HW.m_pSwapChain->Present(0, 0);
#endif
#else //	USE_DX10
    CHK_DX(HW.pDevice->EndScene());

    HW.pDevice->Present(NULL, NULL, NULL, NULL);
#endif //	USE_DX10
       // HRESULT _hr		= HW.pDevice->Present( NULL, NULL, NULL, NULL );
    // if				(D3DERR_DEVICELOST==_hr)	return;			// we will handle this
    // later
}

void dxRenderDeviceRender::ResourcesDestroyNecessaryTextures() {
    Resources->DestroyNecessaryTextures();
}

void dxRenderDeviceRender::ClearTarget() {
#if defined(USE_DX10) || defined(USE_DX11)
    FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    HW.pContext->ClearRenderTargetView(RCache.get_RT(), ColorRGBA);
#else  //	USE_DX10
    CHK_DX(HW.pDevice->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1, 0));
#endif //	USE_DX10
}

void dxRenderDeviceRender::SetCacheXform(Fmatrix& mView, Fmatrix& mProject) {
    RCache.set_xform_view(mView);
    RCache.set_xform_project(mProject);
}

void dxRenderDeviceRender::SetCacheXform_prev(Fmatrix& mView, Fmatrix& mProject) {
    RCache.set_xform_view_prev(mView);
    RCache.set_xform_project_prev(mProject);
}

bool dxRenderDeviceRender::HWSupportsShaderYUV2RGB() {
    u32 v_dev = CAP_VERSION(HW.Caps.raster_major, HW.Caps.raster_minor);
    u32 v_need = CAP_VERSION(2, 0);
    return (v_dev >= v_need);
}

void dxRenderDeviceRender::OnAssetsChanged() {
    Resources->m_textures_description.UnLoad();
    Resources->m_textures_description.Load();
}
