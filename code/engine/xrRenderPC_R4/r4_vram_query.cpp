// Written by Ivan Danko, 2026-11-14: item 9 "full measurement" - the video memory for the frame
// measurement ( "* Memory", xrEngine\device.cpp ): the used and the budget of the card's own memory
// and the used shared memory, from DXGI 1.4 ( Windows 10+; an older Windows answers false ). A file
// of its own without the precompiled header: the renderer's headers are the old DirectX SDK ones,
// DXGI 1.4 needs the Windows SDK's ( CMakeLists.txt: /Y- ).
// Створено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - відеопам'ять для заміру кадру
// ( "* Memory", xrEngine\device.cpp ): зайнята пам'ять і бюджет власної пам'яті відеокарти та
// зайнята спільна пам'ять, з DXGI 1.4 ( Windows 10+; старіша Windows відповідає false ). Окремий
// файл без попередньо скомпільованого заголовка: заголовки рендеру - старі з DirectX SDK, а DXGI 1.4
// потребує заголовків Windows SDK ( CMakeLists.txt: /Y- ).
#include <windows.h>
#include <dxgi1_4.h>

bool r4_vram_query(IUnknown* device, unsigned long long* used, unsigned long long* budget,
                   unsigned long long* nonlocal) {
    if (!device)
        return false;
    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice),
                                      reinterpret_cast<void**>(&dxgi_device))) ||
        !dxgi_device)
        return false;
    IDXGIAdapter* adapter = nullptr;
    const HRESULT got_adapter = dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();
    if (FAILED(got_adapter) || !adapter)
        return false;
    IDXGIAdapter3* adapter3 = nullptr;
    const HRESULT got_adapter3 =
        adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3));
    adapter->Release();
    if (FAILED(got_adapter3) || !adapter3)
        return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO local = {}, shared = {};
    const bool ok =
        SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
        SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared));
    adapter3->Release();
    if (!ok)
        return false;
    *used = local.CurrentUsage;
    *budget = local.Budget;
    *nonlocal = shared.CurrentUsage;
    return true;
}
