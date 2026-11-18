#include "selection.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void setPixel(ps::Bitmap& image,int x,int y,int r,int g,int b) {
    auto* p=image.pixels.data()+(static_cast<size_t>(y)*image.width+x)*4;
    p[0]=static_cast<uint8_t>(b);p[1]=static_cast<uint8_t>(g);p[2]=static_cast<uint8_t>(r);p[3]=255;
}
void saveJpeg(const ps::Bitmap& image,const std::filesystem::path& path) {
    ps::ComPtr<IWICImagingFactory> factory;
    ps::check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        reinterpret_cast<void**>(factory.put())),"JPEG test factory");
    ps::ComPtr<IWICStream> stream;ps::check(factory->CreateStream(stream.put()),"JPEG test stream");
    ps::check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"JPEG test output");
    ps::ComPtr<IWICBitmapEncoder> encoder;ps::check(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,encoder.put()),"JPEG test encoder");
    ps::check(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache),"JPEG test initialize");
    ps::ComPtr<IWICBitmapFrameEncode> frame;ps::ComPtr<IPropertyBag2> options;
    ps::check(encoder->CreateNewFrame(frame.put(),options.put()),"JPEG test frame");
    PROPBAG2 property{};property.pstrName=const_cast<wchar_t*>(L"ImageQuality");
    VARIANT quality{};quality.vt=VT_R4;quality.fltVal=0.65f;
    ps::check(options->Write(1,&property,&quality),"JPEG test quality");
    ps::check(frame->Initialize(options.get()),"JPEG test frame initialize");
    ps::check(frame->SetSize(image.width,image.height),"JPEG test size");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    ps::check(frame->SetPixelFormat(&format),"JPEG test format");
    ps::ComPtr<IWICBitmap> bitmap;
    ps::check(factory->CreateBitmapFromMemory(image.width,image.height,GUID_WICPixelFormat32bppBGRA,image.width*4,
        static_cast<UINT>(image.pixels.size()),const_cast<BYTE*>(image.pixels.data()),bitmap.put()),"JPEG test source");
    ps::ComPtr<IWICFormatConverter> converter;ps::check(factory->CreateFormatConverter(converter.put()),"JPEG test converter");
    ps::check(converter->Initialize(bitmap.get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"JPEG test conversion");
    ps::check(frame->WriteSource(converter.get(),nullptr),"JPEG test pixels");
    ps::check(frame->Commit(),"JPEG test frame commit");ps::check(encoder->Commit(),"JPEG test commit");
}
ps::Bitmap readImage(const std::filesystem::path& path) {
    ps::ComPtr<IWICImagingFactory> factory;
    ps::check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        reinterpret_cast<void**>(factory.put())),"Read test factory");
    ps::ComPtr<IWICBitmapDecoder> decoder;
    ps::check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.put()),"Read test image");
    ps::ComPtr<IWICBitmapFrameDecode> frame;ps::check(decoder->GetFrame(0,frame.put()),"Read test frame");
    UINT width=0,height=0;ps::check(frame->GetSize(&width,&height),"Read test size");
    ps::Bitmap image(width,height);ps::ComPtr<IWICFormatConverter> converter;
    ps::check(factory->CreateFormatConverter(converter.put()),"Read test converter");
    ps::check(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Read test format");
    ps::check(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(image.pixels.size()),image.pixels.data()),"Read test pixels");return image;
}
void checkQuality(const ps::Bitmap& image,const ps::SelectionMask& selection,const char* label) {
    size_t background=0,selectedBackground=0,foreground=0,selectedForeground=0;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        // Exclude the compression/antialias transition from the ground truth.
        if(x>=126&&x<258&&y>=44&&y<214) {
            if(x>=132&&x<252&&y>=50&&y<208){++foreground;if(selection[y*image.width+x])++selectedForeground;}
        } else {++background;if(selection[y*image.width+x])++selectedBackground;}
    }
    const double recall=static_cast<double>(selectedBackground)/background;
    const double spill=static_cast<double>(selectedForeground)/foreground;
    std::cout<<"  "<<label<<": background recall "<<recall*100<<"%, subject spill "<<spill*100<<"%\n";
    require(recall>=0.985,"Gradient/shadow background selection missed too many pixels");
    require(spill<=0.001,"Background selection leaked into colored subject");
}
} // namespace

void testWandQuality(const std::filesystem::path& output) {
    ps::Bitmap scene(384,256);
    for(int y=0;y<scene.height;++y)for(int x=0;x<scene.width;++x) {
        const double dx=(x-278)/74.0,dy=(y-160)/63.0;
        const int noise=((x*29+y*17+(x*y)%23)%9)-4;
        const int gray=std::clamp(static_cast<int>(250-18.0*x/(scene.width-1)-64*std::exp(-(dx*dx+dy*dy)))+noise,0,255);
        setPixel(scene,x,y,gray-2,gray,gray+2);
        if(x>=128&&x<256&&y>=46&&y<212)setPixel(scene,x,y,33,87,181);
    }
    const auto selection=ps::selectSimilar(scene,{12,12},18,true);
    checkQuality(scene,selection,"gradient + shadow + noise");
    ps::savePng(scene,output/"wand-gradient-input.png");
    ps::savePng(ps::transparencyPreview(scene,selection),output/"wand-gradient-selection.png");
    ps::Annotation remove;remove.tool=ps::Tool::Cutout;remove.opacity=ps::cutoutOpacity(selection,scene.width,scene.height,false,true);
    ps::savePng(ps::annotate(scene,{remove}),output/"wand-gradient-cutout.png");
    saveJpeg(scene,output/"wand-compressed.jpg");const auto compressed=readImage(output/"wand-compressed.jpg");
    checkQuality(compressed,ps::selectSimilar(compressed,{12,12},18,true),"JPEG quality 65");
    // The clicked pixel can itself be a mild compression outlier.
    setPixel(scene,12,12,226,228,230);
    checkQuality(scene,ps::selectSimilar(scene,{12,12},18,true),"noisy seed");
    std::cout<<"PASS: gradient, shadow, compression artifacts and noisy click sampling\n";

    // Pale borders are still within the seed's shadow-tolerant color range.
    // Contiguous mode must respect their local edge instead of joining panels.
    ps::Bitmap panels(96,64);
    for(int y=0;y<64;++y)for(int x=0;x<96;++x) {
        const bool border=((x==20||x==75)&&y>=14&&y<=50)||((y==14||y==50)&&x>=20&&x<=75);
        const int gray=border?225:245;setPixel(panels,x,y,gray,gray,gray);
    }
    for(int tolerance:{10,18}) {
        const auto local=ps::selectSimilar(panels,{40,30},tolerance,true);
        const auto global=ps::selectSimilar(panels,{40,30},tolerance,false);
        require(local[30*96+40]&&!local[30*96+10]&&!local[10*96+40],"Contiguous selection crossed a pale UI border");
        require(std::count(local.begin(),local.end(),255)==54*35,"Continuous mode failed to select the full enclosed panel");
        require(global[30*96+10]&&global[10*96+40],"Global mode stopped selecting separated similar colors");
        const auto outside=ps::selectSimilar(panels,{10,30},tolerance,true);
        require(outside[30*96+10]&&!outside[30*96+40],"Continuous mode crossed the same outline from outside");
        if(tolerance==10) {
            ps::savePng(panels,output/"wand-panels-input.png");
            ps::savePng(ps::transparencyPreview(panels,local),output/"wand-panels-contiguous.png");
            ps::savePng(ps::transparencyPreview(panels,global),output/"wand-panels-global.png");
        }
    }
    std::cout<<"PASS: pale UI boundaries isolate contiguous regions while global mode selects both\n";

    ps::Bitmap ramp(256,8);
    for(int y=0;y<8;++y)for(int x=0;x<256;++x)setPixel(ramp,x,y,255-x,255-x,255-x);
    auto mask=ps::selectSimilar(ramp,{0,0},18,true);
    require(mask[0]&&mask[40]&&!mask[180]&&!mask[255],"Gradual gradient drift crossed into unrelated dark colors");
    ps::Bitmap separated(64,32);
    for(int y=0;y<32;++y)for(int x=0;x<64;++x)setPixel(separated,x,y,x==31?0:245,x==31?0:245,x==31?0:245);
    mask=ps::selectSimilar(separated,{5,5},18,true);require(mask[5*64+5]&&!mask[5*64+45],"Contiguous mode crossed a solid barrier");
    mask=ps::selectSimilar(separated,{5,5},18,false);require(mask[5*64+45]&&!mask[5*64+31],"Global mode did not select separated similar background");
    mask=ps::selectSimilar(separated,{31,5},18,true);require(mask[5*64+31]&&!mask[5*64+30],"Neighborhood sampling replaced a one-pixel subject with background");
    separated.pixels[3]=0;mask=ps::selectSimilar(separated,{5,5},100,false);require(!mask[0]&&mask[5*64+31],"Maximum tolerance or alpha exclusion is incorrect");
    ps::Bitmap diagonal(3,3);
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)setPixel(diagonal,x,y,x==y?255:0,x==y?255:0,x==y?255:0);
    mask=ps::selectSimilar(diagonal,{0,0},0,true);require(mask[0]&&!mask[4]&&!mask[8],"Contiguous selection escaped through a diagonal corner");
    mask=ps::selectSimilar(diagonal,{0,0},0,false);require(mask[4]&&mask[8],"Global mode missed diagonal similar colors");
    std::cout<<"PASS: bounded gradient growth, contiguous/global modes, thin features, alpha, diagonal isolation\n";

    std::mt19937 random(4271);
    for(int trial=0;trial<80;++trial) {
        const int w=1+static_cast<int>(random()%45),h=1+static_cast<int>(random()%35);
        ps::Bitmap grid(w,h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const int gray=random()%3?250:20;setPixel(grid,x,y,gray,gray,gray);
            if(random()%9==0)grid.pixels[(y*w+x)*4+3]=0;
        }
        const POINT seed{static_cast<LONG>(random()%w),static_cast<LONG>(random()%h)};
        const size_t start=static_cast<size_t>(seed.y)*w+seed.x;
        ps::SelectionMask expected(static_cast<size_t>(w)*h,0);std::vector<POINT> pending;
        if(grid.pixels[start*4+3]){pending.push_back(seed);expected[start]=255;}
        while(!pending.empty()) {
            const POINT p=pending.back();pending.pop_back();
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                if(std::abs(dx)+std::abs(dy)!=1)continue;
                const int x=p.x+dx,y=p.y+dy;if(x<0||y<0||x>=w||y>=h)continue;
                const size_t i=static_cast<size_t>(y)*w+x;
                if(!expected[i]&&grid.pixels[i*4+3]&&grid.pixels[i*4]==grid.pixels[start*4]) {
                    expected[i]=255;pending.push_back({x,y});
                }
            }
        }
        require(ps::selectSimilar(grid,seed,0,true)==expected,"Scanline fill disagrees with four-neighbor graph traversal");
        const auto preview=ps::transparencyPreview(grid,expected);
        require(preview.width==w&&preview.height==h,"Preview failed on narrow image");
    }
    std::cout<<"PASS: randomized scanline connectivity, transparent barriers, narrow-image preview\n";

    ps::Bitmap fullHd(1920,1080);
    for(int y=0;y<1080;++y)for(int x=0;x<1920;++x)setPixel(fullHd,x,y,240,242,244);
    const auto begin=std::chrono::steady_clock::now();mask=ps::selectSimilar(fullHd,{0,0},18,true);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    require(std::all_of(mask.begin(),mask.end(),[](uint8_t p){return p==255;}),"Scanline fill missed large uniform region");
    std::cout<<"PASS: 1920x1080 selection ("<<elapsed<<" ms)\n";
}
