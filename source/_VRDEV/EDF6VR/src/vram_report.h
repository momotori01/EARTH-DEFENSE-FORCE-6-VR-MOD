// Included by plugin.cpp. The game process's video memory against the budget
// Windows gives it (IDXGIAdapter3::QueryVideoMemoryInfo), sampled twice a second
// on the game loop and logged with the 5 s report. Past the budget Windows moves
// the process's graphics memory to and from system memory, and the GPU waits on
// those copies: a friend's 50 s at 8 fps (2026-11-08) could not be told apart
// from plain GPU load without this.
#pragma once
#include <dxgi1_4.h>
#include <d3d11.h>

namespace vram_report {
struct Window {
    unsigned long long used=0, peak=0, budget=0, lowestBudget=0;
    unsigned long long sharedUsed=0, sharedPeak=0;
    unsigned samples=0, over=0;
};
inline IDXGIAdapter3* g_adapter=nullptr;
inline void* g_device=nullptr;
inline bool g_unsupported=false;
inline ULONGLONG g_lastSample=0;
inline Window g_window{};

inline void Attach(void* device) noexcept {
    if(g_adapter) { g_adapter->Release(); g_adapter=nullptr; }
    g_device=device; g_unsupported=false;
    IDXGIDevice* dxgi=nullptr;
    if(FAILED(static_cast<ID3D11Device*>(device)->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi))) || !dxgi) {
        g_unsupported=true; return;
    }
    IDXGIAdapter* adapter=nullptr;
    if(SUCCEEDED(dxgi->GetAdapter(&adapter)) && adapter) {
        if(FAILED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),reinterpret_cast<void**>(&g_adapter)))) g_adapter=nullptr;
        adapter->Release();
    }
    dxgi->Release();
    if(!g_adapter) g_unsupported=true;
}

// Game loop thread, every update; does its work twice a second.
inline void Poll(void* device,ULONGLONG now) noexcept {
    if(!device || now-g_lastSample<500) return;
    g_lastSample=now;
    if(device!=g_device) Attach(device);
    if(!g_adapter) return;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
    if(FAILED(g_adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local))) return;
    if(FAILED(g_adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared))) shared={};
    auto& w=g_window;
    w.used=local.CurrentUsage; w.budget=local.Budget; w.sharedUsed=shared.CurrentUsage;
    if(local.CurrentUsage>w.peak) w.peak=local.CurrentUsage;
    if(!w.lowestBudget || local.Budget<w.lowestBudget) w.lowestBudget=local.Budget;
    if(shared.CurrentUsage>w.sharedPeak) w.sharedPeak=shared.CurrentUsage;
    if(local.CurrentUsage>local.Budget) ++w.over;
    ++w.samples;
}

// Same thread, from the 5 s report; starts the next window.
template<class Logger> void Report(Logger log) noexcept {
    constexpr unsigned long long mb=1024ull*1024ull;
    const auto& w=g_window;
    if(w.samples)
        log("VRAM used=%lluMB peak=%lluMB budget=%lluMB lowestBudget=%lluMB overBudget=%u/%u samples "
            "shared used=%lluMB peak=%lluMB (this process; budget = what Windows lets it keep in video memory now)",
            w.used/mb,w.peak/mb,w.budget/mb,w.lowestBudget/mb,w.over,w.samples,w.sharedUsed/mb,w.sharedPeak/mb);
    else if(g_unsupported)
        log("VRAM unavailable (no IDXGIAdapter3 on the game's device)");
    g_window={};
}
}
