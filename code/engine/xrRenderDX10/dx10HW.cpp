#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable : 4995)
#include <d3dx9.h>
#pragma warning(default : 4995)
#include "xrRenderCommon/HW.h"
#include "xrEngine/XR_IOConsole.h"
#include "xrAPI/xrAPI.h"
#include <algorithm>

#include "StateManager\dx10SamplerStateCache.h"
#include "StateManager\dx10StateCache.h"

#ifndef _EDITOR
void fill_vid_mode_list(CHW* _hw);
void free_vid_mode_list();

void fill_render_mode_list();
void free_render_mode_list();
#else
void fill_vid_mode_list(CHW* _hw) {}
void free_vid_mode_list() {}
void fill_render_mode_list() {}
void free_render_mode_list() {}
#endif

CHW HW;

CHW::CHW() : m_pAdapter(0), pDevice(NULL), m_move_window(true) {
    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);
}

CHW::~CHW() {
    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

void CHW::CreateD3D() {
    IDXGIFactory* pFactory;
    R_CHK(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory)));

    m_pAdapter = 0;
    m_bUsePerfhud = false;

#ifndef MASTER_GOLD
    UINT i = 0;
    while (pFactory->EnumAdapters(i, &m_pAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC desc;
        m_pAdapter->GetDesc(&desc);
        if (!wcscmp(desc.Description, L"NVIDIA PerfHUD")) {
            m_bUsePerfhud = true;
            break;
        } else {
            m_pAdapter->Release();
            m_pAdapter = 0;
        }
        ++i;
    }
#endif

    if (!m_pAdapter)
        pFactory->EnumAdapters(0, &m_pAdapter);

    pFactory->Release();
}

void CHW::DestroyD3D() {
    _SHOW_REF("refCount:m_pAdapter", m_pAdapter);
    _RELEASE(m_pAdapter);
}

void CHW::CreateDevice(HWND m_hWnd, bool move_window) {
    m_move_window = move_window;
    CreateD3D();

    BOOL bWindowed = !psDeviceFlags.is(rsFullscreen);

    m_DriverType = Caps.bForceGPU_REF ? D3D_DRIVER_TYPE_REFERENCE : D3D_DRIVER_TYPE_HARDWARE;

    if (m_bUsePerfhud) m_DriverType = D3D_DRIVER_TYPE_REFERENCE;

    DXGI_ADAPTER_DESC Desc;
    R_CHK(m_pAdapter->GetDesc(&Desc));
    Msg("* GPU [vendor:%X]-[device:%X]: %S", Desc.VendorId, Desc.DeviceId, Desc.Description);

    Caps.id_vendor = Desc.VendorId;
    Caps.id_device = Desc.DeviceId;

    D3DFORMAT& fTarget = Caps.fTarget;
    D3DFORMAT& fDepth = Caps.fDepth;

    fTarget = D3DFMT_X8R8G8B8; 
    fDepth = selectDepthStencil(fTarget);

    DXGI_SWAP_CHAIN_DESC& sd = m_ChainDesc;
    std::memset(&sd, 0, sizeof(sd));

    selectResolution(sd.BufferDesc.Width, sd.BufferDesc.Height, bWindowed);

    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferCount = 1;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.OutputWindow = m_hWnd;
    sd.Windowed = bWindowed;

    if (bWindowed) {
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
    } else {
        sd.BufferDesc.RefreshRate = selectRefresh(sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferDesc.Format);
    }

    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    UINT createDeviceFlags = 0;
    HRESULT R;

#ifdef USE_DX11
    D3D_FEATURE_LEVEL pFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    
    // КРИТИЧНИЙ ФІКС ДЛЯ DX11: Якщо адаптер знайдено (не NULL), DriverType має бути строго UNKNOWN
    D3D_DRIVER_TYPE creationDriverType = m_pAdapter ? D3D_DRIVER_TYPE_UNKNOWN : m_DriverType;
    
    R = D3D11CreateDeviceAndSwapChain(m_pAdapter, creationDriverType, NULL, createDeviceFlags, pFeatureLevels,
        sizeof(pFeatureLevels) / sizeof(pFeatureLevels[0]), D3D11_SDK_VERSION, &sd, &m_pSwapChain,
        &pDevice, &FeatureLevel, &pContext);
#else
    // ФІКС ДЛЯ DX10: Тут типу UNKNOWN не існує. Використовуємо наш стандартний m_DriverType
    R = D3DX10CreateDeviceAndSwapChain(m_pAdapter, m_DriverType, NULL, createDeviceFlags, &sd, &m_pSwapChain, &pDevice);
    pContext = pDevice;
    FeatureLevel = D3D_FEATURE_LEVEL_10_0;
    if (!FAILED(R)) {
        D3DX10GetFeatureLevel1(pDevice, &pDevice1);
        FeatureLevel = D3D_FEATURE_LEVEL_10_1;
    }
    pContext1 = pDevice1;
#endif

    if (FAILED(R)) {
        Msg("Failed to initialize graphics hardware.\nPlease try to restart the game.\nCreateDevice returned 0x%08x", R);
        FlushLog();
        MessageBox(NULL, "Failed to initialize graphics hardware.\nPlease try to restart the game.", "Error!", MB_OK | MB_ICONERROR);
        TerminateProcess(GetCurrentProcess(), 0);
    };
    R_CHK(R);

    _SHOW_REF("* CREATE: DeviceREF:", HW.pDevice);

    UpdateViews();

    size_t memory = Desc.DedicatedVideoMemory;
    Msg("* Texture memory: %d M", memory / (1024 * 1024));
#ifndef _EDITOR
    updateWindowProps(m_hWnd);
    fill_vid_mode_list(this);
#endif
}

void CHW::DestroyDevice() {
    StateManager.Reset();
    RSManager.ClearStateArray();
    DSSManager.ClearStateArray();
    BSManager.ClearStateArray();
    SSManager.ClearStateArray();

    _SHOW_REF("refCount:pBaseZB", pBaseZB);
    _RELEASE(pBaseZB);

    _SHOW_REF("refCount:pBaseRT", pBaseRT);
    _RELEASE(pBaseRT);

    if (!m_ChainDesc.Windowed) m_pSwapChain->SetFullscreenState(FALSE, NULL);
    _SHOW_REF("refCount:m_pSwapChain", m_pSwapChain);
    _RELEASE(m_pSwapChain);

#ifdef USE_DX11
    _RELEASE(pContext);
#endif
#ifndef USE_DX11
    _RELEASE(HW.pDevice1);
#endif
    _SHOW_REF("DeviceREF:", HW.pDevice);
    _RELEASE(HW.pDevice);

    DestroyD3D();

#ifndef _EDITOR
    free_vid_mode_list();
#endif
}

void CHW::Reset(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC& cd = m_ChainDesc;
    BOOL bWindowed = !psDeviceFlags.is(rsFullscreen);
    cd.Windowed = bWindowed;
    m_pSwapChain->SetFullscreenState(!bWindowed, NULL);

    DXGI_MODE_DESC& desc = m_ChainDesc.BufferDesc;
    selectResolution(desc.Width, desc.Height, bWindowed);

    if (bWindowed) {
        desc.RefreshRate.Numerator = 60;
        desc.RefreshRate.Denominator = 1;
    } else desc.RefreshRate = selectRefresh(desc.Width, desc.Height, desc.Format);

    CHK_DX(m_pSwapChain->ResizeTarget(&desc));

    _SHOW_REF("refCount:pBaseZB", pBaseZB);
    _SHOW_REF("refCount:pBaseRT", pBaseRT);
    _RELEASE(pBaseZB);
    _RELEASE(pBaseRT);

    CHK_DX(m_pSwapChain->ResizeBuffers(cd.BufferCount, desc.Width, desc.Height, desc.Format, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    UpdateViews();
    updateWindowProps(hwnd);
}

D3DFORMAT CHW::selectDepthStencil(D3DFORMAT fTarget) { return D3DFMT_D24S8; }

void CHW::selectResolution(u32& dwWidth, u32& dwHeight, BOOL bWindowed) {
    fill_vid_mode_list(this);
    if (bWindowed) {
        dwWidth = psCurrentVidMode[0];
        dwHeight = psCurrentVidMode[1];
    } else {
        string64 buff;
        xr_sprintf(buff, sizeof(buff), "%dx%d", psCurrentVidMode[0], psCurrentVidMode[1]);
        if (_ParseItem(buff, vid_mode_token) == u32(-1)) { 
            xr_sprintf(buff, sizeof(buff), "vid_mode %s", vid_mode_token[0].name);
            Console->Execute(buff);
        }
        dwWidth = psCurrentVidMode[0];
        dwHeight = psCurrentVidMode[1];
    }
}

DXGI_RATIONAL CHW::selectRefresh(u32 dwWidth, u32 dwHeight, DXGI_FORMAT fmt) {
    DXGI_RATIONAL res;
    res.Numerator = 60;
    res.Denominator = 1;
    float CurrentFreq = 60.0f;

    if (psDeviceFlags.is(rsRefresh60hz)) {
        return res;
    } else {
        xr_vector<DXGI_MODE_DESC> modes;
        IDXGIOutput* pOutput;
        m_pAdapter->EnumOutputs(0, &pOutput);
        VERIFY(pOutput);

        UINT num = 0;
        UINT flags = 0;
        pOutput->GetDisplayModeList(fmt, flags, &num, 0);

        modes.resize(num);
        pOutput->GetDisplayModeList(fmt, flags, &num, &modes.front());
        _RELEASE(pOutput);

        for (u32 i = 0; i < num; ++i) {
            DXGI_MODE_DESC& desc = modes[i];
            if ((desc.Width == dwWidth) && (desc.Height == dwHeight)) {
                VERIFY(desc.RefreshRate.Denominator);
                float TempFreq = float(desc.RefreshRate.Numerator) / float(desc.RefreshRate.Denominator);
                if (TempFreq > CurrentFreq) {
                    CurrentFreq = TempFreq;
                    res = desc.RefreshRate;
                }
            }
        }
        return res;
    }
}

void CHW::OnAppActivate() {
    if (m_pSwapChain && !m_ChainDesc.Windowed) {
        ShowWindow(m_ChainDesc.OutputWindow, SW_RESTORE);
        m_pSwapChain->SetFullscreenState(TRUE, NULL);
    }
}

void CHW::OnAppDeactivate() {
    if (m_pSwapChain && !m_ChainDesc.Windowed) {
        m_pSwapChain->SetFullscreenState(FALSE, NULL);
        ShowWindow(m_ChainDesc.OutputWindow, SW_MINIMIZE);
    }
}

BOOL CHW::support(D3DFORMAT fmt, DWORD type, DWORD usage) { return TRUE; }

void CHW::updateWindowProps(HWND m_hWnd) {
    BOOL bWindowed = TRUE;
#ifndef _EDITOR
    bWindowed = !psDeviceFlags.is(rsFullscreen);
#endif
    u32 dwWindowStyle = 0;
    if (bWindowed) {
        if (m_move_window) {
            if (strstr(Core.Params, "-no_dialog_header"))
                SetWindowLong(m_hWnd, GWL_STYLE, dwWindowStyle = (WS_BORDER | WS_VISIBLE));
            else
                SetWindowLong(m_hWnd, GWL_STYLE, dwWindowStyle = (WS_BORDER | WS_DLGFRAME | WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX));

            RECT m_rcWindowBounds;
            BOOL bCenter = FALSE;
            if (strstr(Core.Params, "-center_screen")) bCenter = TRUE;

            if (bCenter) {
                RECT DesktopRect;
                GetClientRect(GetDesktopWindow(), &DesktopRect);
                SetRect(&m_rcWindowBounds, (DesktopRect.right - m_ChainDesc.BufferDesc.Width) / 2,
                        (DesktopRect.bottom - m_ChainDesc.BufferDesc.Height) / 2,
                        (DesktopRect.right + m_ChainDesc.BufferDesc.Width) / 2,
                        (DesktopRect.bottom + m_ChainDesc.BufferDesc.Height) / 2);
            } else {
                SetRect(&m_rcWindowBounds, 0, 0, m_ChainDesc.BufferDesc.Width, m_ChainDesc.BufferDesc.Height);
            };

            AdjustWindowRect(&m_rcWindowBounds, dwWindowStyle, FALSE);
            SetWindowPos(m_hWnd, HWND_NOTOPMOST, m_rcWindowBounds.left, m_rcWindowBounds.top,
                         (m_rcWindowBounds.right - m_rcWindowBounds.left),
                         (m_rcWindowBounds.bottom - m_rcWindowBounds.top),
                         SWP_SHOWWINDOW | SWP_NOCOPYBITS | SWP_DRAWFRAME);
        }
    } else {
        SetWindowLong(m_hWnd, GWL_STYLE, dwWindowStyle = (WS_POPUP | WS_VISIBLE));
    }
    ShowCursor(FALSE);
    SetForegroundWindow(m_hWnd);
}

#ifndef _EDITOR
void free_vid_mode_list() {
    if (!vid_mode_token) return;
    for (int i = 0; vid_mode_token[i].name; i++) {
        xr_free(vid_mode_token[i].name);
    }
    xr_free(vid_mode_token);
    vid_mode_token = NULL;
}

void fill_vid_mode_list(CHW* _hw) {
    if (vid_mode_token != NULL) return;
    xr_vector<LPCSTR> _tmp;
    xr_vector<DXGI_MODE_DESC> modes;

    IDXGIOutput* pOutput;
    _hw->m_pAdapter->EnumOutputs(0, &pOutput);
    VERIFY(pOutput);

    UINT num = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT flags = 0;

    pOutput->GetDisplayModeList(format, flags, &num, 0);
    modes.resize(num);
    pOutput->GetDisplayModeList(format, flags, &num, &modes.front());
    _RELEASE(pOutput);

    for (u32 i = 0; i < num; ++i) {
        DXGI_MODE_DESC& desc = modes[i];
        string32 str;

        if (desc.Width < 800) continue;
        xr_sprintf(str, sizeof(str), "%dx%d", desc.Width, desc.Height);

        // ОПТИМІЗАЦІЯ: Лямбда-функція замість макросу/структури _uniq_mode
        if (_tmp.end() != std::find_if(_tmp.begin(), _tmp.end(), [&str](LPCSTR other) { return stricmp(str, other) == 0; }))
            continue;

        _tmp.push_back(NULL);
        _tmp.back() = xr_strdup(str);
    }

    u32 _cnt = _tmp.size() + 1;
    vid_mode_token = xr_alloc<xr_token>(_cnt);
    vid_mode_token[_cnt - 1].id = -1;
    vid_mode_token[_cnt - 1].name = NULL;

    for (u32 i = 0; i < _tmp.size(); ++i) {
        vid_mode_token[i].id = i;
        vid_mode_token[i].name = _tmp[i];
    }
}

void CHW::UpdateViews() {
    DXGI_SWAP_CHAIN_DESC& sd = m_ChainDesc;
    HRESULT R;

    ID3DTexture2D* pBuffer;
    R = m_pSwapChain->GetBuffer(0, __uuidof(ID3DTexture2D), (LPVOID*)&pBuffer);
    R_CHK(R);

    R = pDevice->CreateRenderTargetView(pBuffer, NULL, &pBaseRT);
    pBuffer->Release();
    R_CHK(R);

    ID3DTexture2D* pDepthStencil = NULL;
    D3D_TEXTURE2D_DESC descDepth;
    descDepth.Width = sd.BufferDesc.Width;
    descDepth.Height = sd.BufferDesc.Height;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D_USAGE_DEFAULT;
    descDepth.BindFlags = D3D_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    R = pDevice->CreateTexture2D(&descDepth, NULL, &pDepthStencil);
    R_CHK(R);

    R = pDevice->CreateDepthStencilView(pDepthStencil, NULL, &pBaseZB);
    R_CHK(R);

    pDepthStencil->Release();
}
#endif