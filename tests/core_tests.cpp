#include "image.h"
#include "recorder.h"
#include "selection.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void testWandQuality(const std::filesystem::path& output);
void testSelectionSessions();
void testSmartCutout();
void require(bool value,const char* explanation) {if(!value)throw std::runtime_error(explanation);}
ps::Bitmap decode(const std::filesystem::path& file,UINT frameIndex=0,UINT* count=nullptr,unsigned* delay=nullptr) {
    ps::ComPtr<IWICImagingFactory> factory;
    ps::check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.put())),"Test factory");
    ps::ComPtr<IWICBitmapDecoder> decoder;
    ps::check(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.put()),"Decode saved image");
    if(count)ps::check(decoder->GetFrameCount(count),"Frame count");
    ps::ComPtr<IWICBitmapFrameDecode> frame;ps::check(decoder->GetFrame(frameIndex,frame.put()),"Decode frame");
    if(delay) {
        ps::ComPtr<IWICMetadataQueryReader> metadata;ps::check(frame->GetMetadataQueryReader(metadata.put()),"Read GIF frame metadata");
        PROPVARIANT value{};ps::check(metadata->GetMetadataByName(L"/grctlext/Delay",&value),"Read GIF delay");
        require(value.vt==VT_UI2,"GIF delay has incorrect type");*delay=value.uiVal;PropVariantClear(&value);
    }
    UINT width=0,height=0;ps::check(frame->GetSize(&width,&height),"Read dimensions");ps::Bitmap bitmap(width,height);
    ps::ComPtr<IWICFormatConverter> converter;ps::check(factory->CreateFormatConverter(converter.put()),"Test converter");
    ps::check(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Decode BGRA");
    ps::check(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(bitmap.pixels.size()),bitmap.pixels.data()),"Decoded pixels");return bitmap;
}
ps::Bitmap solid(int w,int h,uint8_t red,uint8_t green,uint8_t blue) {
    ps::Bitmap result(w,h);for(size_t i=0;i<result.pixels.size();i+=4){result.pixels[i]=blue;result.pixels[i+1]=green;result.pixels[i+2]=red;result.pixels[i+3]=255;}return result;
}
void testDesktop(const std::filesystem::path& output) {
    auto desktop=ps::desktopRect();auto screen=ps::capture({desktop.left,desktop.top,desktop.left+32,desktop.top+32});
    require(screen.width==32&&screen.height==32&&screen.pixels[3]==255,"Desktop capture failed");
    std::cout<<"PASS: real Win32 desktop capture\n";
    const auto recording=output/"recording.gif";std::filesystem::remove(recording);
    ps::Recorder recorder;recorder.start({desktop.left,desktop.top,desktop.left+160,desktop.top+90},recording,nullptr,WM_APP+9);
    std::this_thread::sleep_for(std::chrono::milliseconds(650));recorder.stop();recorder.join();
    require(recorder.error.empty(),recorder.error.c_str());
    UINT count=0;auto read=decode(recording,0,&count);
    require(read.width==160&&read.height==90&&count>=1,"Live recording produced invalid frames");
    require(recorder.frames==count,"Live recording frame count does not match decoded file");
    std::cout<<"PASS: live recording worker, stop, GIF finalization and decode\n";
}
int main(int argc,char** argv) {
    try {
        ps::ComApartment apartment;
        auto output=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(output);
        if(argc==2&&std::string(argv[1])=="--desktop"){testDesktop(output);return 0;}
        if(argc!=1)throw std::runtime_error("Usage: capture_tests [--desktop]");
        const auto red=solid(96,64,255,0,0),blue=solid(96,64,0,0,255);
        ps::savePng(red,output/"roundtrip.png");auto read=decode(output/"roundtrip.png");
        require(read.width==96&&read.height==64&&read.pixels==red.pixels,"PNG did not preserve dimensions and pixels");
        // A user's explicit overwrite must replace the existing PNG completely.
        ps::savePng(blue,output/"roundtrip.png");read=decode(output/"roundtrip.png");require(read.pixels==blue.pixels,"PNG overwrite failed");
        std::cout<<"PASS: lossless PNG encode/decode and overwrite\n";
        auto transparent=solid(96,64,255,0,0);
        transparent.pixels[3]=0;transparent.pixels[7]=128;transparent.pixels[11]=255;
        ps::savePng(transparent,output/"alpha-roundtrip.png");read=decode(output/"alpha-roundtrip.png");
        require(read.pixels==transparent.pixels,"PNG did not preserve straight RGB and 0/128/255 alpha values");
        std::cout<<"PASS: real PNG transparency and partial alpha roundtrip\n";
        auto sample=solid(12,8,255,255,255);
        // Black divider separates identical white background regions. A nearby
        // off-white pixel distinguishes tolerance matching from exact equality.
        for(int y=0;y<8;++y)for(int c=0;c<3;++c)sample.pixels[(y*12+5)*4+c]=0;
        for(int c=0;c<3;++c)sample.pixels[(1*12+1)*4+c]=245;
        auto connected=ps::selectSimilar(sample,{0,0},0,true);
        auto global=ps::selectSimilar(sample,{0,0},0,false);
        require(connected[2*12+2]==255&&connected[2*12+8]==0&&connected[1*12+1]==0,"Contiguous magic wand leaked or ignored exact tolerance");
        require(global[2*12+8]==255&&global[2*12+5]==0,"Global similar-color selection failed");
        auto tolerant=ps::selectSimilar(sample,{0,0},5,true);require(tolerant[1*12+1]==255,"Magic wand tolerance failed");
        ps::combineSelection(connected,ps::selectSimilar(sample,{8,2},0,true),false);require(connected[2*12+8]==255,"Add selection failed");
        ps::combineSelection(connected,global,true);require(connected[2*12+8]==0&&connected[2*12+2]==0,"Subtract selection failed");
        ps::invertSelection(connected,sample);require(connected[0]==255,"Invert selection failed");
        sample.pixels[3]=0;require(ps::selectSimilar(sample,{0,0},100,false)[0]==0,"Transparent pixels should not seed color selection");
        std::cout<<"PASS: connected/global magic wand, tolerance, add/subtract/invert, transparent seed\n";
        testWandQuality(output);
        testSelectionSessions();
        testSmartCutout();
        auto icon=solid(48,48,255,255,255);
        for(int y=12;y<36;++y)for(int x=12;x<36;++x){const size_t i=(y*48+x)*4;icon.pixels[i]=200;icon.pixels[i+1]=80;icon.pixels[i+2]=20;}
        const auto background=ps::selectSimilar(icon,{0,0},0,true);
        ps::Annotation remove;remove.tool=ps::Tool::Cutout;remove.opacity=ps::cutoutOpacity(background,48,48,false,false);
        auto cutout=ps::annotate(icon,{remove});require(cutout.pixels[3]==0&&cutout.pixels[(20*48+20)*4+3]==255,"Cutout did not remove background and preserve subject");
        require(cutout.pixels[0]==0&&cutout.pixels[1]==0&&cutout.pixels[2]==0,"Fully transparent pixels retain hidden color bleed");
        ps::savePng(cutout,output/"transparent-cutout.png");read=decode(output/"transparent-cutout.png");require(read.pixels==cutout.pixels,"Saved cutout lost transparency");
        const auto original=ps::annotate(icon,{});require(original.pixels==icon.pixels,"Empty edit history should restore unmodified image");
        ps::Annotation mark;mark.tool=ps::Tool::Rectangle;mark.points={{16,16},{30,30}};
        auto marked=ps::annotate(icon,{remove,mark});require(marked.pixels[3]==0,"Drawing after cutout destroyed transparency");
        ps::Annotation keep=remove;keep.opacity=ps::cutoutOpacity(background,48,48,true,false);
        auto kept=ps::annotate(icon,{keep});require(kept.pixels[3]==255&&kept.pixels[(20*48+20)*4+3]==0,"Keep selection applied inverse mask");
        ps::Annotation feather=remove;feather.opacity=ps::cutoutOpacity(background,48,48,false,true);
        auto softened=ps::annotate(icon,{feather});const unsigned edgeAlpha=softened.pixels[(12*48+12)*4+3];
        require(edgeAlpha>0&&edgeAlpha<255,"One-pixel soft edge did not create partial transparency");
        require(softened.pixels[(12*48+11)*4+3]==0,"Soft edges restored removed background and created a fringe");
        auto keptMask=ps::cutoutOpacity(background,48,48,true,true);
        require(keptMask[12*48+12]==0,"Soft keep mask expanded beyond the selected region");
        const auto preview=ps::transparencyPreview(cutout,background);
        require(preview.pixels[3]==255&&cutout.pixels[3]==0,"Checkerboard preview modified export alpha");
        std::cout<<"PASS: remove/keep cutout, transparent annotation, undo source, soft edges, non-destructive preview\n";
        const auto gif=output/"animation.gif";std::filesystem::remove(gif);
        {ps::GifWriter writer(gif);writer.frame(red,10);writer.frame(blue,25);writer.finish();}
        UINT count=0;unsigned delay=0;read=decode(gif,0,&count,&delay);
        require(count==2&&delay==10&&read.pixels==red.pixels,"First GIF frame or delay is incorrect");
        read=decode(gif,1,nullptr,&delay);require(delay==25&&read.pixels==blue.pixels,"GIF local palettes or second delay is incorrect");
        require(!std::filesystem::exists(gif.wstring()+L".tmp"),"GIF temporary output was not finalized");
        std::cout<<"PASS: animated GIF frames, independent palettes, timing, finalization\n";
        const auto abandoned=output/"abandoned.gif";std::filesystem::remove(abandoned);
        {ps::GifWriter writer(abandoned);writer.frame(red,10);}
        require(!std::filesystem::exists(abandoned)&&!std::filesystem::exists(abandoned.wstring()+L".tmp"),"Abandoned recording left a partial file");
        std::cout<<"PASS: interrupted encoder cleanup\n";
        auto piece=ps::crop(blue,{12,7,43,28});require(piece.width==31&&piece.height==21&&piece.pixels[0]==255,"Crop offset or dimensions incorrect");
        auto scaled=ps::fitWidth(blue,48);require(scaled.width==48&&scaled.height==32,"GIF resize aspect ratio incorrect");
        bool rejected=false;try {ps::crop(red,{-1,0,30,30});}catch(const std::exception&){rejected=true;}require(rejected,"Out-of-bounds crop was accepted");
        std::cout<<"PASS: physical-pixel crop, bounds, GIF resize\n";
        ps::Annotation rectangle;rectangle.tool=ps::Tool::Rectangle;rectangle.points={{5,5},{70,50}};
        auto annotated=ps::annotate(blue,{rectangle});require(annotated.pixels!=blue.pixels,"Direct2D annotation was not drawn");
        require(annotated.pixels[(60*96+90)*4]==255,"Annotation changed pixels outside its bounds");
        ps::Annotation label;label.tool=ps::Tool::Text;label.points={{8,8}};label.text=L"Text";
        label.textSize=12;
        const auto smallText=ps::annotate(blue,{label});
        const RECT smallBounds=ps::textAnnotationBounds(label,label.points.front(),blue.width,blue.height);
        label.textSize=30;label.color=0x28D440;
        const auto largeText=ps::annotate(blue,{label});
        const RECT largeBounds=ps::textAnnotationBounds(label,label.points.front(),blue.width,blue.height);
        require(largeBounds.right-largeBounds.left>smallBounds.right-smallBounds.left&&
            largeBounds.bottom-largeBounds.top>smallBounds.bottom-smallBounds.top,
            "Text hit region ignored font size");
        require(smallText.pixels!=blue.pixels&&largeText.pixels!=smallText.pixels,"Size/color controls did not change rendered text");
        int maxGreen=0;
        for(size_t i=1;i<largeText.pixels.size();i+=4)maxGreen=std::max<int>(maxGreen,largeText.pixels[i]);
        require(maxGreen>160,"Custom text color was not passed to the renderer");
        auto gradient=solid(48,48,0,0,0);for(int y=0;y<48;++y)for(int x=0;x<48;++x)gradient.pixels[(y*48+x)*4]=static_cast<uint8_t>(x*5);
        ps::Annotation mosaic;mosaic.tool=ps::Tool::Mosaic;mosaic.points={{0,0},{24,24}};
        auto masked=ps::annotate(gradient,{mosaic});require(masked.pixels[0]==masked.pixels[4*11],"Mosaic did not replace a block");
        require(masked.pixels[4*30]==gradient.pixels[4*30],"Mosaic modified pixels outside selection");
        mosaic.mosaicBlock=4;const auto fine=ps::annotate(gradient,{mosaic});
        mosaic.mosaicBlock=64;const auto coarse=ps::annotate(gradient,{mosaic});
        require(fine.pixels[0]==fine.pixels[4*3]&&fine.pixels[0]!=fine.pixels[4*4],
            "Fine mosaic ignored the configured block size");
        require(coarse.pixels[0]==coarse.pixels[4*24]&&coarse.pixels[4*25]==gradient.pixels[4*25],
            "Coarse mosaic skipped the final column or covered unselected pixels");
        std::cout<<"PASS: Direct2D shapes, text size/color, and configurable bounded mosaic\n";
        std::cout<<"All image and selection checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
