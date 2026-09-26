#pragma once
#include <Windows.h>
#include <cstdint>
#include "image_profile.h"

namespace edf6vr {
// What the game's swapchain looks like, read on the render thread.
struct FrameCapture {
    void* swapChain=nullptr;        // IDXGISwapChain*
    void* device=nullptr;           // ID3D11Device*
    void* context=nullptr;          // ID3D11DeviceContext*
    void* window=nullptr;           // HWND the swapchain presents to
    unsigned width=0, height=0;
    unsigned format=0;              // DXGI_FORMAT
    unsigned sampleCount=0, sampleQuality=0;
    unsigned bufferCount=0;
    unsigned flags=0;
    unsigned syncInterval=0, presentFlags=0;
    int windowed=-1;
    unsigned long long presents=0, present1Calls=0;
    bool valid=false;
};

using CaptureLogger=void (*)(const char* text);

// Replaces IDXGISwapChain::Present and Present1 in the DXGI vtable, which is
// shared by every swapchain of that class in the process. The vtable is read
// from a throwaway swapchain of our own, and each slot is only replaced after
// the pointer it holds is confirmed to live inside dxgi.dll.
//
// The hook itself is a pure pass-through: it bumps a counter and forwards. It
// never logs and never allocates, because 0.4.0 lost the process by working
// inside the game's first present. With describeSwapChain the description is
// read once, on frame 600, straight into the struct above; the game thread
// prints it later. Pass false for a hook that makes no COM calls at all.
//
// Never install this during plugin load: 0.4.0 did, and the game then failed to
// start at all. Install it from an explicit in-game action so a bad hook can
// only spoil the session the user chose to test.
bool InstallPresentCapture(CaptureLogger,bool describeSwapChain=true) noexcept;
// Called on the render thread once per present, with the swapchain's back
// buffer already resolved to an ID3D11Texture2D*. Set it to null to stop.
using FrameCallback=void (*)(void* backBuffer,unsigned width,unsigned height,unsigned sampleCount);
void SetFrameCallback(FrameCallback) noexcept;
// Refreshed by live mission updates; expires automatically if updates stop.
void SetPresentUncapped(bool) noexcept;
unsigned RequestedSyncInterval() noexcept;

// Replaces EDF.dll's import of D3D11CreateDevice so the game's own device and
// immediate context are known from the moment it makes them. This creates
// nothing of its own, which is what made the 0.4.0 load-time probe fatal; it
// only forwards the call and keeps a reference to the result.
bool InstallDeviceCapture(const ImageProfile&,CaptureLogger) noexcept;
bool DeviceCaptureInstalled() noexcept;
// The game's device and immediate context, or nulls before it has made them.
bool GameDevice(void*& device,void*& context) noexcept;

// The thread the game presents on. Equal to the game loop thread means the
// renderer is inline, which is what a two-pass stereo render needs.
unsigned long PresentThread() noexcept;
bool PresentCaptureInstalled() noexcept;
bool ReadFrameCapture(FrameCapture&) noexcept;
const char* PresentCaptureStatus() noexcept;
}
