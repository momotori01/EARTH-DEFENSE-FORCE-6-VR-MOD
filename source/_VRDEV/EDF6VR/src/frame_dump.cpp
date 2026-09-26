#include "frame_dump.h"
#include "depth_probe.h"

#include <d3d11.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace edf6vr {
namespace {
std::atomic<bool> g_wanted{false};
std::atomic<unsigned> g_serial{0};

// A staging copy of whatever is being written out. One at a time, rebuilt when
// the shape changes, and released as soon as the write is done: this runs only
// when asked for, so nothing needs to be kept warm.
ID3D11Texture2D* MakeStaging(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& source) noexcept {
    D3D11_TEXTURE2D_DESC desc=source;
    desc.MipLevels=1;
    desc.ArraySize=1;
    desc.SampleDesc.Count=1;
    desc.SampleDesc.Quality=0;
    desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    desc.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return nullptr;
    return staging;
}

bool ReadBack(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,
              ID3D11Texture2D*& staging,D3D11_MAPPED_SUBRESOURCE& mapped,
              D3D11_TEXTURE2D_DESC& desc) noexcept {
    if(!source) return false;
    source->GetDesc(&desc);
    if(desc.SampleDesc.Count!=1) return false;
    staging=MakeStaging(device,desc);
    if(!staging) return false;
    context->CopyResource(staging,source);
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)) || !mapped.pData) {
        staging->Release(); staging=nullptr;
        return false;
    }
    return true;
}

// 24-bit bottom-up BMP, which needs no library and opens anywhere.
bool WriteBmp(const wchar_t* path,const unsigned char* pixels,unsigned width,unsigned height,
              unsigned pitch,bool blueFirst) noexcept {
    FILE* file=nullptr;
    if(_wfopen_s(&file,path,L"wb") || !file) return false;
    const unsigned rowBytes=width*3;
    const unsigned padding=(4-(rowBytes%4))%4;
    const unsigned imageBytes=(rowBytes+padding)*height;
    unsigned char header[54]{};
    header[0]='B'; header[1]='M';
    const unsigned fileBytes=54+imageBytes;
    std::memcpy(header+2,&fileBytes,4);
    const unsigned offset=54; std::memcpy(header+10,&offset,4);
    const unsigned infoSize=40; std::memcpy(header+14,&infoSize,4);
    std::memcpy(header+18,&width,4);
    std::memcpy(header+22,&height,4);
    const unsigned short planes=1; std::memcpy(header+26,&planes,2);
    const unsigned short bits=24; std::memcpy(header+28,&bits,2);
    std::memcpy(header+34,&imageBytes,4);
    std::fwrite(header,1,sizeof(header),file);
    auto row=new(std::nothrow) unsigned char[rowBytes+padding];
    if(!row) { std::fclose(file); return false; }
    std::memset(row,0,rowBytes+padding);
    for(unsigned y=0;y<height;++y) {
        const auto scan=pixels+static_cast<size_t>(height-1-y)*pitch;
        for(unsigned x=0;x<width;++x) {
            const auto pixel=scan+static_cast<size_t>(x)*4;
            // BMP wants blue first; the source is either RGBA or BGRA.
            row[x*3+0]=blueFirst?pixel[0]:pixel[2];
            row[x*3+1]=pixel[1];
            row[x*3+2]=blueFirst?pixel[2]:pixel[0];
        }
        std::fwrite(row,1,rowBytes+padding,file);
    }
    delete[] row;
    std::fclose(file);
    return true;
}

bool WriteRaw(const wchar_t* path,const unsigned char* pixels,unsigned width,unsigned height,
              unsigned pitch) noexcept {
    FILE* file=nullptr;
    if(_wfopen_s(&file,path,L"wb") || !file) return false;
    for(unsigned y=0;y<height;++y)
        std::fwrite(pixels+static_cast<size_t>(y)*pitch,4,width,file);
    std::fclose(file);
    return true;
}

// The scene colour EDF6 draws into is R16G16B16A16_FLOAT and linear, while the
// finished frame is eight bit and tone mapped. Writing the first as bytes gave
// nonsense, which is why the mask that compares them has never been looked at.
float HalfToFloat(unsigned short value) noexcept {
    const unsigned sign=(value>>15)&1, exponent=(value>>10)&0x1F, mantissa=value&0x3FF;
    unsigned bits=0;
    if(!exponent) {
        if(!mantissa) bits=sign<<31;
        else {
            unsigned e=exponent, m=mantissa;
            do { m<<=1; --e; } while(!(m&0x400));
            m&=0x3FF;
            bits=(sign<<31)|((e+127-15+1)<<23)|(m<<13);
        }
    } else if(exponent==0x1F) bits=(sign<<31)|0x7F800000u|(mantissa<<13);
    else bits=(sign<<31)|((exponent+127-15)<<23)|(mantissa<<13);
    float out=0;
    std::memcpy(&out,&bits,4);
    return out;
}

unsigned char LinearToByte(float value) noexcept {
    if(!(value>0)) return 0;
    // The usual sRGB transfer, so a linear image comes out looking like the
    // finished one rather than uniformly dark.
    const float encoded=value<=0.0031308f?value*12.92f
                                         :1.055f*std::pow(value,1.0f/2.4f)-0.055f;
    const int byte=static_cast<int>(encoded*255.0f+0.5f);
    return static_cast<unsigned char>(byte<0?0:(byte>255?255:byte));
}

bool IsHalfFloat(DXGI_FORMAT format) noexcept {
    return format==DXGI_FORMAT_R16G16B16A16_FLOAT || format==DXGI_FORMAT_R16G16B16A16_TYPELESS;
}

bool WriteBmpHalf(const wchar_t* path,const unsigned char* pixels,unsigned width,unsigned height,
                  unsigned pitch) noexcept {
    auto bytes=new(std::nothrow) unsigned char[static_cast<size_t>(width)*height*4];
    if(!bytes) return false;
    for(unsigned y=0;y<height;++y) {
        auto scan=reinterpret_cast<const unsigned short*>(pixels+static_cast<size_t>(y)*pitch);
        for(unsigned x=0;x<width;++x) {
            auto out=bytes+(static_cast<size_t>(y)*width+x)*4;
            for(int c=0;c<3;++c) out[c]=LinearToByte(HalfToFloat(scan[x*4+c]));
            out[3]=255;
        }
    }
    const bool ok=WriteBmp(path,bytes,width,height,width*4,false);
    delete[] bytes;
    return ok;
}

bool IsBlueFirst(DXGI_FORMAT format) noexcept {
    return format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
           || format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
}

void DumpColour(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,
                const wchar_t* directory,const wchar_t* name,unsigned serial,
                CaptureLogger log,bool preserveAlpha=false) noexcept {
    if(!source) return;
    ID3D11Texture2D* staging=nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    D3D11_TEXTURE2D_DESC desc{};
    if(!ReadBack(device,context,source,staging,mapped,desc)) return;
    wchar_t path[MAX_PATH]{};
    _snwprintf_s(path,MAX_PATH,_TRUNCATE,L"%s%s_%u.bmp",directory,name,serial);
    const bool ok=IsHalfFloat(desc.Format)
        ?WriteBmpHalf(path,static_cast<const unsigned char*>(mapped.pData),
                      desc.Width,desc.Height,mapped.RowPitch)
        :WriteBmp(path,static_cast<const unsigned char*>(mapped.pData),
                  desc.Width,desc.Height,mapped.RowPitch,IsBlueFirst(desc.Format));
    if(preserveAlpha && !IsHalfFloat(desc.Format)) {
        _snwprintf_s(path,MAX_PATH,_TRUNCATE,L"%s%s_%u.raw",directory,name,serial);
        const bool raw=WriteRaw(path,static_cast<const unsigned char*>(mapped.pData),
                               desc.Width,desc.Height,mapped.RowPitch);
        unsigned rgb=0,alpha=0;
        for(unsigned y=0;y<desc.Height;++y) {
            const auto row=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch;
            for(unsigned x=0;x<desc.Width;++x) {
                const auto p=row+x*4;rgb+=(p[0]||p[1]||p[2])?1:0;alpha+=p[3]?1:0;
            }
        }
        if(log) {
            char report[320]{};
            std::snprintf(report,sizeof(report),"RETICLEDUMP %ls_%u.raw %ux%u fmt=%u rgbPixels=%u alphaPixels=%u written=%d",
                name,serial,desc.Width,desc.Height,desc.Format,rgb,alpha,raw);log(report);
        }
    }
    context->Unmap(staging,0);
    staging->Release();
    if(log) {
        char text[320]{};
        std::snprintf(text,sizeof(text),"%ls_%u.bmp %ux%u fmt=%u %s",
                      name,serial,desc.Width,desc.Height,desc.Format,ok?"written":"failed");
        log(text);
    }
}
}

void RequestFrameDump() noexcept { g_wanted.store(true,std::memory_order_relaxed); }
bool FrameDumpRequested() noexcept { return g_wanted.load(std::memory_order_relaxed); }

void WriteFrameDump(ID3D11Device* device,ID3D11DeviceContext* context,
                    ID3D11Texture2D* colour,ID3D11Texture2D* depth,ID3D11Texture2D* hudless,
                    ID3D11Texture2D* preUi,const wchar_t* directory,CaptureLogger log,
                    ID3D11Texture2D* directUi,ID3D11Texture2D* reticle) noexcept {
    if(!g_wanted.exchange(false,std::memory_order_relaxed)) return;
    if(!device || !context || !directory) return;
    const unsigned serial=g_serial.fetch_add(1,std::memory_order_relaxed);
    __try {
        DumpColour(device,context,colour,directory,L"dump_colour",serial,log);
        DumpColour(device,context,hudless,directory,L"dump_hudless",serial,log);
        DumpColour(device,context,preUi,directory,L"dump_preui",serial,log);
        DumpColour(device,context,directUi,directory,L"dump_direct_ui",serial,log,true);
        DumpColour(device,context,reticle,directory,L"dump_aim_reticle",serial,log,true);
        // All of them, named by which bind they came from. Which one is the
        // picture before the HUD is the whole question, and one dump answers it
        // instead of a build each time to try the next index.
        const int held=PreUiHeld();
        for(int i=0;i<held;++i) {
            wchar_t name[32]{};
            _snwprintf_s(name,32,_TRUNCATE,L"dump_bind%d",i);
            DumpColour(device,context,PreUiSnapshot(i),directory,name,serial,log);
        }
        if(depth) {
            ID3D11Texture2D* staging=nullptr;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            D3D11_TEXTURE2D_DESC desc{};
            if(ReadBack(device,context,depth,staging,mapped,desc)) {
                wchar_t path[MAX_PATH]{};
                _snwprintf_s(path,MAX_PATH,_TRUNCATE,L"%sdump_depth_%u.raw",directory,serial);
                const bool ok=WriteRaw(path,static_cast<const unsigned char*>(mapped.pData),
                                       desc.Width,desc.Height,mapped.RowPitch);
                context->Unmap(staging,0);
                staging->Release();
                if(log) {
                    char text[320]{};
                    std::snprintf(text,sizeof(text),
                                  "dump_depth_%u.raw %ux%u float32 rows top to bottom %s",
                                  serial,desc.Width,desc.Height,ok?"written":"failed");
                    log(text);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(log) log("frame dump raised an exception");
    }
}
}
