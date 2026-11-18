#include "image.h"
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>

namespace ps {
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream out;
        out << operation << " (0x" << std::hex << static_cast<unsigned long>(hr) << ')';
        throw std::runtime_error(out.str());
    }
}
Bitmap::Bitmap(int w, int h) : width(w), height(h) {
    const auto bytes = static_cast<uint64_t>(w > 0 ? w : 0) * static_cast<uint64_t>(h > 0 ? h : 0) * 4;
    if (w <= 0 || h <= 0 || bytes > 512ULL * 1024 * 1024) throw std::runtime_error("Invalid or excessively large capture area");
    pixels.resize(static_cast<size_t>(bytes));
}
RECT desktopRect() {
    const LONG x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}
RECT normalized(POINT a, POINT b) { return {std::min(a.x,b.x), std::min(a.y,b.y), std::max(a.x,b.x), std::max(a.y,b.y)}; }
namespace {
struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    void* bits = nullptr;
    Dib(int width, int height) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bitmap) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            throw std::runtime_error("Cannot allocate screen bitmap");
        }
        old = SelectObject(dc, bitmap);
    }
    ~Dib() { if (old) SelectObject(dc,old); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
};
void createFactory(ComPtr<IWICImagingFactory>& f) {
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWICImagingFactory, reinterpret_cast<void**>(f.put())), "Create WIC factory");
}
void encodePng(IWICImagingFactory* f, IStream* stream, const Bitmap& bitmap) {
    ComPtr<IWICBitmapEncoder> encoder;
    check(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()), "Create PNG encoder");
    check(encoder->Initialize(stream,WICBitmapEncoderNoCache), "Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.put(),nullptr), "Create PNG frame");
    check(frame->Initialize(nullptr), "Initialize PNG frame");
    check(frame->SetSize(bitmap.width,bitmap.height), "PNG dimensions");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "PNG pixel format");
    ComPtr<IWICBitmap> source;
    check(f->CreateBitmapFromMemory(bitmap.width,bitmap.height,GUID_WICPixelFormat32bppBGRA,
          bitmap.width*4,static_cast<UINT>(bitmap.pixels.size()),const_cast<BYTE*>(bitmap.pixels.data()),source.put()), "PNG bitmap");
    check(frame->WriteSource(source.get(),nullptr), "Write PNG pixels");
    check(frame->Commit(), "Commit PNG frame");
    check(encoder->Commit(), "Commit PNG");
}
void setByteMetadata(IWICMetadataQueryWriter* writer, LPCWSTR key, BYTE value) {
    PROPVARIANT v{}; v.vt=VT_UI1; v.bVal=value;
    check(writer->SetMetadataByName(key,&v), "GIF metadata");
}
void setVectorMetadata(IWICMetadataQueryWriter* writer, LPCWSTR key, BYTE* bytes, ULONG size) {
    PROPVARIANT v{}; v.vt=VT_VECTOR|VT_UI1; v.caub.cElems=size; v.caub.pElems=bytes;
    check(writer->SetMetadataByName(key,&v), "GIF loop metadata");
}
bool openClipboard(HWND owner) {
    for(int n=0;n<8;++n) { if(OpenClipboard(owner)) return true; Sleep(20); }
    return false;
}
HGLOBAL globalBytes(const void* data, size_t size) {
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);
    if(!memory) throw std::bad_alloc();
    void* p=GlobalLock(memory);
    if(!p) { GlobalFree(memory); throw std::runtime_error("Cannot lock clipboard memory"); }
    std::memcpy(p,data,size); GlobalUnlock(memory); return memory;
}
} // namespace
Bitmap capture(RECT r, bool cursor) {
    Bitmap result(r.right-r.left,r.bottom-r.top);
    Dib dib(result.width,result.height);
    HDC screen=GetDC(nullptr);
    if(!screen) throw std::runtime_error("Cannot access desktop");
    BOOL ok=BitBlt(dib.dc,0,0,result.width,result.height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);
    if(!ok) ok=BitBlt(dib.dc,0,0,result.width,result.height,screen,r.left,r.top,SRCCOPY);
    const DWORD captureError=ok?ERROR_SUCCESS:GetLastError();
    ReleaseDC(nullptr,screen);
    if(!ok) throw std::runtime_error("Screen capture failed (Win32 error "+std::to_string(captureError)+")");
    if(cursor) {
        CURSORINFO ci{};ci.cbSize=sizeof(ci);
        if(GetCursorInfo(&ci) && (ci.flags&CURSOR_SHOWING)) {
            ICONINFO icon{};
            if(GetIconInfo(ci.hCursor,&icon)) {
                DrawIconEx(dib.dc,ci.ptScreenPos.x-r.left-static_cast<int>(icon.xHotspot),
                    ci.ptScreenPos.y-r.top-static_cast<int>(icon.yHotspot),ci.hCursor,0,0,0,nullptr,DI_NORMAL);
                if(icon.hbmMask) DeleteObject(icon.hbmMask);
                if(icon.hbmColor) DeleteObject(icon.hbmColor);
            }
        }
    }
    GdiFlush();
    std::memcpy(result.pixels.data(),dib.bits,result.pixels.size());
    for(size_t i=3;i<result.pixels.size();i+=4) result.pixels[i]=255;
    return result;
}
Bitmap crop(const Bitmap& source, RECT r) {
    if(r.left<0||r.top<0||r.right>source.width||r.bottom>source.height) throw std::runtime_error("Capture outside desktop");
    Bitmap result(r.right-r.left,r.bottom-r.top);
    for(int y=0;y<result.height;++y)
        std::memcpy(result.pixels.data()+static_cast<size_t>(y)*result.width*4,
                    source.pixels.data()+(static_cast<size_t>(y+r.top)*source.width+r.left)*4,result.width*4);
    return result;
}
Bitmap fitWidth(const Bitmap& source, int maxWidth) {
    if(maxWidth<=0 || source.width<=maxWidth) return source;
    const int height=std::max(1,static_cast<int>(std::lround(static_cast<double>(source.height)*maxWidth/source.width)));
    Bitmap result(maxWidth,height);
    Dib input(source.width,source.height), output(result.width,result.height);
    std::memcpy(input.bits,source.pixels.data(),source.pixels.size());
    SetStretchBltMode(output.dc,HALFTONE); SetBrushOrgEx(output.dc,0,0,nullptr);
    if(!StretchBlt(output.dc,0,0,result.width,result.height,input.dc,0,0,source.width,source.height,SRCCOPY))
        throw std::runtime_error("GIF resize failed");
    GdiFlush(); std::memcpy(result.pixels.data(),output.bits,result.pixels.size());
    for(size_t i=3;i<result.pixels.size();i+=4) result.pixels[i]=255;
    return result;
}
Bitmap dimmed(const Bitmap& source) {
    Bitmap result=source;
    for(size_t i=0;i<result.pixels.size();i+=4)
        for(int c=0;c<3;++c) result.pixels[i+c]=static_cast<uint8_t>(result.pixels[i+c]*0.40);
    return result;
}
void drawBitmap(HDC dc,const Bitmap& bitmap,int x,int y) {
    if(bitmap.empty()) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=bitmap.width;
    info.bmiHeader.biHeight=-bitmap.height; info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    SetDIBitsToDevice(dc,x,y,bitmap.width,bitmap.height,0,0,0,bitmap.height,bitmap.pixels.data(),&info,DIB_RGB_COLORS);
}
Bitmap annotate(const Bitmap& base,const std::vector<Annotation>& items) {
    if(items.empty()) return base;
    Bitmap result=base;
    for(size_t i=0;i<result.pixels.size();i+=4)
        for(int c=0;c<3;++c)result.pixels[i+c]=static_cast<uint8_t>((result.pixels[i+c]*result.pixels[i+3]+127)/255);
    ComPtr<IWICImagingFactory> wic;createFactory(wic);
    ComPtr<IWICBitmap> surface;
    check(wic->CreateBitmapFromMemory(base.width,base.height,GUID_WICPixelFormat32bppPBGRA,base.width*4,
        static_cast<UINT>(result.pixels.size()),result.pixels.data(),surface.put()),"Create alpha annotation surface");
    ComPtr<ID2D1Factory> d2d;
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.put()), "Create Direct2D factory");
    ComPtr<ID2D1RenderTarget> target;
    auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    const auto bind=[&]{check(d2d->CreateWicBitmapRenderTarget(surface.get(),props,target.put()),"Create transparent annotation renderer");};
    bind();
    ComPtr<IDWriteFactory> dw;
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dw.put())),"Create text renderer");
    ComPtr<IDWriteTextFormat> format;
    check(dw->CreateTextFormat(L"Microsoft YaHei UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,22,L"zh-CN",format.put()),"Create text format");
    for(const auto& a:items) {
        if(a.tool==Tool::Cutout) {
            if(a.opacity.size()!=static_cast<size_t>(base.width)*base.height)throw std::runtime_error("Invalid cutout mask");
            target.reset();
            ComPtr<IWICBitmapLock> locked;check(surface->Lock(nullptr,WICBitmapLockRead|WICBitmapLockWrite,locked.put()),"Lock cutout surface");
            UINT stride=0,size=0;BYTE* pixels=nullptr;
            check(locked->GetStride(&stride),"Cutout stride");check(locked->GetDataPointer(&size,&pixels),"Cutout pixels");
            for(int y=0;y<base.height;++y)for(int x=0;x<base.width;++x) {
                const size_t index=static_cast<size_t>(y)*base.width+x,offset=static_cast<size_t>(y)*stride+x*4;
                for(int c=0;c<4;++c)pixels[offset+c]=static_cast<uint8_t>((pixels[offset+c]*a.opacity[index]+127)/255);
            }
            locked.reset();bind();continue;
        }
        if(a.points.empty()) continue;
        if(a.tool==Tool::Mosaic && a.points.size()>1) {
            RECT r=normalized(a.points.front(),a.points.back());
            r.left=std::clamp<LONG>(r.left,0,base.width); r.right=std::clamp<LONG>(r.right,0,base.width);
            r.top=std::clamp<LONG>(r.top,0,base.height); r.bottom=std::clamp<LONG>(r.bottom,0,base.height);
            target.reset();
            ComPtr<IWICBitmapLock> locked;check(surface->Lock(nullptr,WICBitmapLockRead|WICBitmapLockWrite,locked.put()),"Lock mosaic surface");
            UINT stride=0,size=0;BYTE* pixels=nullptr;
            check(locked->GetStride(&stride),"Mosaic stride");check(locked->GetDataPointer(&size,&pixels),"Mosaic pixels");
            for(int y=r.top;y<r.bottom;y+=12) for(int x=r.left;x<r.right;x+=12) {
                uint64_t sums[4]{};
                for(int yy=y;yy<std::min(y+12,static_cast<int>(r.bottom));++yy)
                    for(int xx=x;xx<std::min(x+12,static_cast<int>(r.right));++xx) {
                        const size_t i=static_cast<size_t>(yy)*stride+xx*4;
                        for(int c=0;c<4;++c) sums[c]+=pixels[i+c];
                    }
                for(int yy=y;yy<std::min(y+12,static_cast<int>(r.bottom));++yy)
                    for(int xx=x;xx<std::min(x+12,static_cast<int>(r.right));++xx) {
                        const size_t i=static_cast<size_t>(yy)*stride+xx*4;
                        for(int c=0;c<3;++c)pixels[i+c]=sums[3]?static_cast<uint8_t>((sums[c]*pixels[i+3]+sums[3]/2)/sums[3]):0;
                    }
            }
            locked.reset();bind();
            continue;
        }
        target->BeginDraw();
        ComPtr<ID2D1SolidColorBrush> brush;
        check(target->CreateSolidColorBrush(D2D1::ColorF(a.color),brush.put()),"Annotation brush");
        const auto p=[](POINT point){return D2D1::Point2F(static_cast<float>(point.x),static_cast<float>(point.y));};
        POINT begin=a.points.front(),end=a.points.back();
        if(a.tool==Tool::Rectangle) {
            RECT r=normalized(begin,end);
            target->DrawRectangle(D2D1::RectF(static_cast<float>(r.left),static_cast<float>(r.top),
                static_cast<float>(r.right),static_cast<float>(r.bottom)),brush.get(),a.thickness);
        } else if(a.tool==Tool::Arrow) {
            target->DrawLine(p(begin),p(end),brush.get(),a.thickness);
            const double angle=std::atan2(end.y-begin.y,end.x-begin.x),length=14;
            for(double offset:{-0.55,0.55})
                target->DrawLine(p(end),D2D1::Point2F(static_cast<float>(end.x-length*std::cos(angle+offset)),
                    static_cast<float>(end.y-length*std::sin(angle+offset))),brush.get(),a.thickness);
        } else if(a.tool==Tool::Pen) {
            if(a.points.size()==1) target->FillEllipse(D2D1::Ellipse(p(begin),a.thickness/2,a.thickness/2),brush.get());
            for(size_t i=1;i<a.points.size();++i) target->DrawLine(p(a.points[i-1]),p(a.points[i]),brush.get(),a.thickness);
        } else if(a.tool==Tool::Text) {
            target->DrawText(a.text.c_str(),static_cast<UINT>(a.text.size()),format.get(),
                D2D1::RectF(static_cast<float>(begin.x),static_cast<float>(begin.y),static_cast<float>(base.width),static_cast<float>(base.height)),brush.get());
        }
        check(target->EndDraw(),"Render annotation");
    }
    target.reset();
    check(surface->CopyPixels(nullptr,base.width*4,static_cast<UINT>(result.pixels.size()),result.pixels.data()),"Read transparent annotations");
    for(size_t i=0;i<result.pixels.size();i+=4) {
        const unsigned alpha=result.pixels[i+3];
        for(int c=0;c<3;++c)result.pixels[i+c]=alpha?static_cast<uint8_t>(std::min(255u,(result.pixels[i+c]*255u+alpha/2)/alpha)):0;
    }
    return result;
}
std::vector<uint8_t> pngBytes(const Bitmap& bitmap) {
    ComPtr<IWICImagingFactory> f; createFactory(f);
    ComPtr<IStream> stream; check(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()),"PNG memory stream");
    encodePng(f.get(),stream.get(),bitmap);
    HGLOBAL data=nullptr; check(GetHGlobalFromStream(stream.get(),&data),"PNG stream data");
    STATSTG stat{}; check(stream->Stat(&stat,STATFLAG_NONAME),"PNG stream size");
    const auto* ptr=static_cast<const uint8_t*>(GlobalLock(data));
    if(!ptr) throw std::runtime_error("Cannot read PNG stream");
    std::vector<uint8_t> bytes(ptr,ptr+static_cast<size_t>(stat.cbSize.QuadPart));
    GlobalUnlock(data); return bytes;
}
void savePng(const Bitmap& bitmap,const std::filesystem::path& path) {
    const auto bytes=pngBytes(bitmap);
    const auto temporary=path.wstring()+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create PNG output file");
    DWORD written=0;
    const BOOL ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
    const bool complete=ok && written==bytes.size();
    CloseHandle(file);
    if(!complete) { DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot write PNG output file"); }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot finalize PNG output file");
    }
}
void copyImage(HWND owner,const Bitmap& bitmap) {
    const auto png=pngBytes(bitmap);
    std::vector<uint8_t> dibV5(sizeof(BITMAPV5HEADER)+bitmap.pixels.size());
    BITMAPV5HEADER v5{};v5.bV5Size=sizeof(v5);v5.bV5Width=bitmap.width;v5.bV5Height=bitmap.height;
    v5.bV5Planes=1;v5.bV5BitCount=32;v5.bV5Compression=BI_BITFIELDS;v5.bV5SizeImage=static_cast<DWORD>(bitmap.pixels.size());
    v5.bV5RedMask=0x00FF0000;v5.bV5GreenMask=0x0000FF00;v5.bV5BlueMask=0x000000FF;v5.bV5AlphaMask=0xFF000000;
    v5.bV5CSType=0x73524742;v5.bV5Intent=LCS_GM_IMAGES; // LCS_sRGB, without MinGW's multi-character literal warning.
    std::memcpy(dibV5.data(),&v5,sizeof(v5));
    std::vector<uint8_t> dib(sizeof(BITMAPINFOHEADER)+bitmap.pixels.size());
    BITMAPINFOHEADER header{}; header.biSize=sizeof(header); header.biWidth=bitmap.width;
    header.biHeight=bitmap.height; header.biPlanes=1; header.biBitCount=32; header.biCompression=BI_RGB;
    header.biSizeImage=static_cast<DWORD>(bitmap.pixels.size());
    std::memcpy(dib.data(),&header,sizeof(header));
    for(int y=0;y<bitmap.height;++y)for(int x=0;x<bitmap.width;++x) {
        const size_t source=(static_cast<size_t>(bitmap.height-1-y)*bitmap.width+x)*4;
        const size_t dest=(static_cast<size_t>(y)*bitmap.width+x)*4;
        std::memcpy(dibV5.data()+sizeof(v5)+dest,bitmap.pixels.data()+source,4);
        const unsigned alpha=bitmap.pixels[source+3];
        for(int c=0;c<3;++c)dib[sizeof(header)+dest+c]=static_cast<uint8_t>((bitmap.pixels[source+c]*alpha+255*(255-alpha)+127)/255);
        dib[sizeof(header)+dest+3]=255;
    }
    std::array<HGLOBAL,3> memory{};
    const auto freeMemory=[&]{for(auto allocation:memory)if(allocation)GlobalFree(allocation);};
    try{memory[0]=globalBytes(png.data(),png.size());memory[1]=globalBytes(dibV5.data(),dibV5.size());memory[2]=globalBytes(dib.data(),dib.size());}
    catch(...){freeMemory();throw;}
    if(!openClipboard(owner)){freeMemory();throw std::runtime_error("Clipboard is busy");}
    if(!EmptyClipboard()){CloseClipboard();freeMemory();throw std::runtime_error("Cannot update clipboard");}
    const std::array<UINT,3> formats={RegisterClipboardFormatW(L"PNG"),CF_DIBV5,CF_DIB};
    bool ok=false;
    for(size_t i=0;i<memory.size();++i)if(formats[i]&&SetClipboardData(formats[i],memory[i])){memory[i]=nullptr;ok=true;}
    CloseClipboard();
    freeMemory();
    if(!ok) throw std::runtime_error("Cannot copy screenshot");
}
void copyFile(HWND owner,const std::filesystem::path& path) {
    const std::wstring name=path.wstring();
    std::vector<uint8_t> data(sizeof(DROPFILES)+(name.size()+2)*sizeof(wchar_t),0);
    DROPFILES header{}; header.pFiles=sizeof(DROPFILES); header.fWide=TRUE;
    std::memcpy(data.data(),&header,sizeof(header));
    std::memcpy(data.data()+sizeof(header),name.data(),name.size()*sizeof(wchar_t));
    HGLOBAL memory=globalBytes(data.data(),data.size());
    if(!openClipboard(owner)) { GlobalFree(memory); throw std::runtime_error("Clipboard is busy"); }
    bool ok=EmptyClipboard() && SetClipboardData(CF_HDROP,memory);
    CloseClipboard(); if(!ok) { GlobalFree(memory); throw std::runtime_error("Cannot copy GIF file"); }
}
struct GifWriter::Impl {
    std::filesystem::path destination,temporary;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    int width=0,height=0,frames=0;
    bool finished=false;
    explicit Impl(const std::filesystem::path& path):destination(path),temporary(path.wstring()+L".tmp") {
        if(std::filesystem::exists(destination)||std::filesystem::exists(temporary)) throw std::runtime_error("GIF output already exists");
        try {
        createFactory(factory);
        check(factory->CreateStream(stream.put()),"GIF stream");
        check(stream->InitializeFromFilename(temporary.c_str(),GENERIC_WRITE),"Open GIF output");
        check(factory->CreateEncoder(GUID_ContainerFormatGif,nullptr,encoder.put()),"GIF encoder");
        check(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache),"Initialize GIF encoder");
        ComPtr<IWICMetadataQueryWriter> metadata;
        check(encoder->GetMetadataQueryWriter(metadata.put()),"GIF loop writer");
        BYTE application[]={'N','E','T','S','C','A','P','E','2','.','0'};
        BYTE looping[]={3,1,0,0,0};
        setVectorMetadata(metadata.get(),L"/appext/Application",application,sizeof(application));
        setVectorMetadata(metadata.get(),L"/appext/Data",looping,sizeof(looping));
        } catch(...) {
            encoder.reset();stream.reset();DeleteFileW(temporary.c_str());throw;
        }
    }
    ~Impl() {
        encoder.reset(); stream.reset();
        if(!finished) DeleteFileW(temporary.c_str());
    }
};
GifWriter::GifWriter(const std::filesystem::path& destination):impl_(std::make_unique<Impl>(destination)) {}
GifWriter::~GifWriter()=default;
void GifWriter::frame(const Bitmap& bitmap,unsigned delay) {
    auto& s=*impl_;
    if(s.finished) throw std::runtime_error("GIF already finalized");
    if(s.frames && (bitmap.width!=s.width||bitmap.height!=s.height)) throw std::runtime_error("GIF frame dimensions changed");
    s.width=bitmap.width; s.height=bitmap.height;
    ComPtr<IWICBitmap> source;
    check(s.factory->CreateBitmapFromMemory(bitmap.width,bitmap.height,GUID_WICPixelFormat32bppBGRA,bitmap.width*4,
        static_cast<UINT>(bitmap.pixels.size()),const_cast<BYTE*>(bitmap.pixels.data()),source.put()),"GIF bitmap");
    ComPtr<IWICPalette> palette; check(s.factory->CreatePalette(palette.put()),"GIF palette");
    check(palette->InitializeFromBitmap(source.get(),256,FALSE),"GIF color quantization");
    ComPtr<IWICFormatConverter> indexed; check(s.factory->CreateFormatConverter(indexed.put()),"GIF converter");
    check(indexed->Initialize(source.get(),GUID_WICPixelFormat8bppIndexed,WICBitmapDitherTypeErrorDiffusion,
        palette.get(),0,WICBitmapPaletteTypeCustom),"GIF indexed colors");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(s.encoder->CreateNewFrame(frame.put(),nullptr),"Create GIF frame");
    check(frame->Initialize(nullptr),"Initialize GIF frame");
    check(frame->SetSize(bitmap.width,bitmap.height),"GIF dimensions");
    WICPixelFormatGUID pixelFormat=GUID_WICPixelFormat8bppIndexed;
    check(frame->SetPixelFormat(&pixelFormat),"GIF pixel format");
    check(frame->SetPalette(palette.get()),"GIF local palette");
    ComPtr<IWICMetadataQueryWriter> metadata;
    check(frame->GetMetadataQueryWriter(metadata.put()),"GIF frame metadata");
    PROPVARIANT time{}; time.vt=VT_UI2; time.uiVal=static_cast<USHORT>(std::clamp(delay,1u,65535u));
    check(metadata->SetMetadataByName(L"/grctlext/Delay",&time),"GIF frame delay");
    setByteMetadata(metadata.get(),L"/grctlext/Disposal",1);
    check(frame->WriteSource(indexed.get(),nullptr),"Write GIF frame");
    check(frame->Commit(),"Commit GIF frame"); ++s.frames;
}
void GifWriter::finish() {
    auto& s=*impl_; if(s.finished) return;
    if(!s.frames) throw std::runtime_error("Recording contains no frames");
    check(s.encoder->Commit(),"Finalize GIF"); s.encoder.reset(); s.stream.reset();
    if(!MoveFileExW(s.temporary.c_str(),s.destination.c_str(),MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot save GIF");
    s.finished=true;
}
} // namespace ps
