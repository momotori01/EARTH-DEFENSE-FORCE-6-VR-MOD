#pragma once
#include <Windows.h>
#include "render_capture.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace edf6vr {
// Writes what the warp is working from to disk, so a question about the picture
// can be answered by looking at the pixels instead of by another trip into the
// headset. The colour goes out as a BMP, the depth as raw floats in the game's
// own units, which is enough to run the whole warp again offline and try
// something different without the game being involved at all.
void RequestFrameDump() noexcept;
bool FrameDumpRequested() noexcept;
// Called on the render thread while the textures are live. Does nothing unless
// a dump has been asked for.
void WriteFrameDump(ID3D11Device*,ID3D11DeviceContext*,
                    ID3D11Texture2D* colour,ID3D11Texture2D* depth,ID3D11Texture2D* hudless,
                    ID3D11Texture2D* preUi,const wchar_t* directory,CaptureLogger,
                    ID3D11Texture2D* directUi=nullptr,ID3D11Texture2D* reticle=nullptr) noexcept;
}
