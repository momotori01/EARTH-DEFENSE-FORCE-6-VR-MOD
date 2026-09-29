#pragma once
// The GPU half of eye_check.h: reads strips of both native eye pictures back
// (two frames in a row, every five seconds, never waiting on the GPU) and logs
// an EYECHECK line. Render thread only.
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace edf6vr {
using EyeCheckLog=void (*)(const char* text);
void EyeCheckFrame(ID3D11DeviceContext* ctx,ID3D11Texture2D* left,ID3D11Texture2D* right,EyeCheckLog log) noexcept;
// Drops the staging textures (device lost, session ended).
void ResetEyeCheck() noexcept;
}
