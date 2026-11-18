// Exercise the real App and its message loop, including the asynchronous hide
// before capture. Keeping this in a separate test target leaves the exe's
// entry point and production behavior unchanged.
#include "../src/main.cpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace {
constexpr COLORREF BACKGROUND=RGB(17,83,151);
void require(bool value,const char* explanation) {
    if(!value)throw std::runtime_error(explanation);
}
LRESULT CALLBACK backgroundProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);
        fill(dc,area,BACKGROUND);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
template<class Predicate> void pumpUntil(Predicate ready) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT)throw std::runtime_error("App exited during capture");
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(ready())return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Timed out waiting for capture");
}
void composeDesktop() {
    const auto readyAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
    pumpUntil([&]{return std::chrono::steady_clock::now()>=readyAt;});DwmFlush();
}
bool showsBackground(const ps::Bitmap& image,RECT area,RECT desktop) {
    for(int y=area.top;y<area.bottom;y+=5)for(int x=area.left;x<area.right;x+=5) {
        const size_t index=(static_cast<size_t>(y-desktop.top)*image.width+x-desktop.left)*4;
        if(image.pixels[index]!=GetBValue(BACKGROUND)||image.pixels[index+1]!=GetGValue(BACKGROUND)||
           image.pixels[index+2]!=GetRValue(BACKGROUND))return false;
    }
    return true;
}
void testWandInteraction() {
    App app;app.state=State::Editing;app.wandMode=true;
    app.selection={340,220,404,252};app.base=ps::Bitmap(64,32);
    for(int y=0;y<32;++y)for(int x=0;x<64;++x) {
        auto* pixel=app.base.pixels.data()+(y*64+x)*4;
        const uint8_t gray=x==31?0:static_cast<uint8_t>(250-y*2);
        pixel[0]=pixel[1]=pixel[2]=gray;pixel[3]=255;
    }
    app.rendered=app.base;
    app.selectMagic({345,225});
    require(app.magicMask[5*64+5]&&!app.magicMask[5*64+45],"Click did not sample crop-local coordinates");
    require(app.magicMask[28*64+5],"Click missed shaded background");
    const auto firstMask=app.magicMask;
    app.tolerance=0;app.recomputeMagicSelection();
    require(app.magicMask[5*64+5]&&!app.magicMask[28*64+5],"Tolerance did not recompute the clicked selection");
    app.tolerance=18;app.recomputeMagicSelection();require(app.magicMask==firstMask,"Restored tolerance changed seed semantics");
    app.contiguous=false;app.recomputeMagicSelection();
    require(app.magicMask[5*64+45]&&!app.magicMask[5*64+31],"Global mode did not update existing click");
    app.contiguous=true;app.recomputeMagicSelection();
    app.selectMagic({385,225},false,true);
    require(app.magicMask[5*64+5]&&app.magicMask[5*64+45],"Shift addition replaced previous selection");
    app.selectMagic({345,225},true,false);
    require(!app.magicMask[5*64+5]&&app.magicMask[5*64+45],"Alt subtraction removed the wrong component");
    app.undo();require(app.magicMask[5*64+5]&&app.magicMask[5*64+45],"Undo did not restore subtracted selection");
    app.undo();require(app.magicMask==firstMask,"Undo did not remove the last added seed");
    app.selectMagic({385,225},false,true);app.selectMagic({345,225},true,false);
    require(app.rendered.pixels==app.base.pixels,"Selection preview changed source pixels");
    app.applyCutout(false);
    require(app.rendered.pixels[(5*64+45)*4+3]==0&&app.rendered.pixels[(5*64+5)*4+3]==255,
        "Remove did not apply the visible combined mask");
    require(app.magicMask.empty()&&!app.selectedPixels,"Applied mask was not cleared");
    app.undo();require(app.rendered.pixels==app.base.pixels,"Undo did not restore original pixels");
    app.selectMagic({345,225});app.applyCutout(true);
    require(app.rendered.pixels[(5*64+5)*4+3]==255&&app.rendered.pixels[(5*64+45)*4+3]==0,
        "Keep did not preserve only the selected component");
    app.undo();app.selectMagic({345,225});app.tolerance=0;app.wandUpdatePending=true;
    app.applyCutout(false);
    require(app.rendered.pixels[(5*64+5)*4+3]==0&&app.rendered.pixels[(28*64+5)*4+3]==255,
        "Apply used stale selection while a tolerance update was pending");
    app.clearMagicSelection();app.annotations.clear();app.base=ps::Bitmap(96,64);
    app.selection={340,220,436,284};app.tolerance=10;app.contiguous=true;
    for(int y=0;y<64;++y)for(int x=0;x<96;++x) {
        const bool border=((x==20||x==75)&&y>=14&&y<=50)||((y==14||y==50)&&x>=20&&x<=75);
        auto* pixel=app.base.pixels.data()+(y*96+x)*4;
        pixel[0]=pixel[1]=pixel[2]=border?225:245;pixel[3]=255;
    }
    app.rendered=app.base;app.selectMagic({380,250});
    require(app.magicMask[30*96+40]&&!app.magicMask[30*96+10],"App continuous mode crossed a pale panel outline");
    app.contiguous=false;app.recomputeMagicSelection();
    require(app.magicMask[30*96+10],"App global mode did not expand existing seed outside the outline");
    app.contiguous=true;app.recomputeMagicSelection();
    require(!app.magicMask[30*96+10],"Switching back to continuous mode left global pixels selected");
    std::cout<<"PASS: crop-local click, live tolerance, continuous/global switch, add/subtract, preview, remove/keep, undo\n";
}
void testAccumulatingLogoSelection() {
    App app;app.state=State::Editing;app.wandMode=true;
    app.selection={340,220,436,284};app.base=ps::Bitmap(96,64);
    for(int y=0;y<64;++y)for(int x=0;x<96;++x) {
        const bool logo=x>=20&&x<76&&y>=12&&y<52;
        const bool yellow=logo&&x>=56&&x<68&&y>=16&&y<28;
        const bool white=logo&&x>=28&&x<40&&y>=24&&y<44;
        auto* pixel=app.base.pixels.data()+(y*96+x)*4;
        pixel[0]=yellow?72:white?255:logo?164:245;
        pixel[1]=yellow?211:white?255:logo?183:245;
        pixel[2]=yellow?251:white?255:logo?24:245;pixel[3]=255;
    }
    app.rendered=app.base;
    constexpr POINT teal{364,236},yellow{400,240},white{372,252};
    constexpr size_t tealIndex=16*96+24,yellowIndex=20*96+60,whiteIndex=32*96+32;
    app.selectMagic(teal);const auto first=app.magicMask;const size_t firstCount=app.selectedPixels;
    app.selectMagic(yellow);
    require(app.magicMask[tealIndex]&&app.magicMask[yellowIndex]&&app.selectedPixels>firstCount,
        "Second plain click replaced the first logo color");
    for(size_t i=0;i<first.size();++i)require(!first[i]||app.magicMask[i],"Plain click lost part of previous selection");
    app.selectMagic(white);
    require(app.magicMask[tealIndex]&&app.magicMask[yellowIndex]&&app.magicMask[whiteIndex]&&!app.magicMask[0],
        "Third logo color did not accumulate independently from the background");
    require(app.selectedPixels==56*40,"Accumulated colors do not cover the complete logo");
    app.selectMagic(white);require(app.wandSeeds.size()==3,"Repeated identical click added a no-op undo step");
    app.undo();require(app.magicMask[tealIndex]&&app.magicMask[yellowIndex]&&!app.magicMask[whiteIndex],
        "Undo removed earlier colors instead of the latest logo selection");
    app.selectMagic(white);app.selectMagic(yellow,true,false);
    require(app.magicMask[tealIndex]&&!app.magicMask[yellowIndex]&&app.magicMask[whiteIndex],
        "Alt click did not subtract only the clicked logo color");
    app.undo();require(app.selectedPixels==56*40,"Undo did not restore subtracted logo color");
    app.tolerance=12;app.recomputeMagicSelection();
    require(app.selectedPixels==56*40,"Tolerance update discarded accumulated colors");
    app.selectMagic(white,false,false);
    require(!app.magicMask[tealIndex]&&!app.magicMask[yellowIndex]&&app.magicMask[whiteIndex]&&app.wandSeeds.size()==1,
        "Ctrl click did not replace the accumulated selection");
    app.clearMagicSelection();require(app.magicMask.empty()&&app.wandSeeds.empty()&&!app.selectedPixels,
        "Clear left accumulated points behind");
    app.selectMagic(teal);app.selectMagic(yellow);app.selectMagic(white);
    const auto output=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(output);
    ps::savePng(app.base,output/"wand-multicolor-logo-input.png");
    ps::savePng(app.preview,output/"wand-multicolor-logo-selection.png");
    app.applyCutout(true);
    for(int y=0;y<64;++y)for(int x=0;x<96;++x) {
        const bool logo=x>=20&&x<76&&y>=12&&y<52;
        require((app.rendered.pixels[(y*96+x)*4+3]!=0)==logo,"Keep did not export the whole accumulated logo with transparent background");
    }
    require(app.rendered.pixels[tealIndex*4+3]==255&&app.rendered.pixels[yellowIndex*4+3]==255&&app.rendered.pixels[whiteIndex*4+3]==255,
        "Keep lost one of the accumulated logo colors");
    ps::savePng(app.rendered,output/"wand-multicolor-logo-cutout.png");
    std::cout<<"PASS: multiple plain clicks accumulate logo colors, repeat/undo/subtract/replace/clear, tolerance, full transparent cutout\n";
}
void testBrushSelection() {
    App app;app.state=State::Editing;app.wandMode=true;app.brushRadius=3;
    app.selection={340,220,468,284};app.base=ps::Bitmap(128,64);
    for(int y=0;y<64;++y)for(int x=0;x<128;++x) {
        const int band=y>=12&&y<52&&x>=20&&x<104?(x-20)/28:-1;
        auto* pixel=app.base.pixels.data()+(y*128+x)*4;
        pixel[0]=band==0?170:band==1?55:band==2?200:245;
        pixel[1]=band==0?175:band==1?210:band==2?60:245;
        pixel[2]=band==0?20:band==1?245:band==2?100:245;pixel[3]=255;
    }
    app.rendered=app.base;
    const POINT from{366,252},to{436,252};
    app.beginMagicStroke(from);app.move(to);app.up(to);
    require(app.selectedPixels==84*40&&!app.magicMask[0],"Fast brush stroke missed a color or crossed the painted bands");
    require(app.magicMask[14*128+60]&&app.magicMask[50*128+90],"Brush selected only its footprint instead of expanding similar colors");
    require(app.wandSeeds.size()==1&&app.wandSeeds.back().path.size()==2,"One brush stroke was split into multiple undo actions");
    const auto full=app.magicMask;
    app.tolerance=12;app.recomputeMagicSelection();require(app.magicMask==full,"Tolerance replay lost a brush stroke");
    app.contiguous=false;app.recomputeMagicSelection();require(app.magicMask==full,"Global replay lost accumulated brush colors");
    app.contiguous=true;app.recomputeMagicSelection();
    app.beginMagicStroke({397,246},true,false);app.move({402,260});app.up({402,260});
    require(app.selectedPixels==56*40&&app.magicMask[30*128+30]&&!app.magicMask[30*128+60]&&app.magicMask[30*128+90],
        "Alt drag did not remove only the touched color");
    app.undo();require(app.magicMask==full,"Undo did not restore an entire subtractive brush stroke");
    const auto historySize=app.wandSeeds.size();
    app.beginMagicStroke(from);app.move({374,255});app.up({374,255});
    require(app.wandSeeds.size()==historySize&&app.magicMask==full,"No-op stroke added a redundant undo step");
    app.undo();require(app.selectedPixels==0,"Undo did not remove the entire additive brush stroke");
    app.selectMagic(from);const auto earlier=app.magicMask;
    app.beginMagicStroke({397,252});app.move(to);app.up(to);app.undo();
    require(app.magicMask==earlier,"Stroke undo discarded an earlier click selection");
    app.beginMagicStroke({397,252},false,false);app.move({399,254});app.up({399,254});
    require(app.selectedPixels==28*40&&!app.magicMask[30*128+30]&&app.magicMask[30*128+60],"Ctrl brush did not start a fresh selection");
    app.beginMagicStroke(from);app.clearMagicSelection();require(app.drag==Drag::None&&app.wandSeeds.empty(),"Clear did not cancel active brush capture");
    app.adjustBrush(1000);require(app.brushRadius==64,"Brush exceeded maximum size");
    app.adjustBrush(-1000);require(app.brushRadius==1,"Brush fell below minimum size");
    app.brushRadius=3;app.beginMagicStroke(from);app.move(to);app.applyCutout(true);
    require(app.drag==Drag::None&&app.rendered.pixels[3]==0&&app.rendered.pixels[(30*128+30)*4+3]==255&&
        app.rendered.pixels[(30*128+60)*4+3]==255&&app.rendered.pixels[(30*128+90)*4+3]==255,
        "Keeping an active stroke did not finish capture and preserve every selected color");
    const auto output=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(output);
    ps::savePng(app.rendered,output/"wand-brush-cutout.png");

    ps::Bitmap halves(12,12);
    for(int y=0;y<12;++y)for(int x=0;x<12;++x) {
        auto* pixel=halves.pixels.data()+(y*12+x)*4;
        pixel[0]=0;pixel[1]=x<6?0:255;pixel[2]=x<6?255:0;pixel[3]=255;
    }
    ps::SelectionMask small,large;
    ps::brushSimilar(halves,small,{4,6},{4,6},1,18,true,false);
    ps::brushSimilar(halves,large,{4,6},{4,6},3,18,true,false);
    require(small[6*12+4]&&!small[6*12+8]&&large[6*12+8],"Brush diameter did not control the sampled circle");
    require(!ps::brushSimilar(halves,large,{-1000000,-1000000},{-999999,-999999},3,18,true,false),"Off-image stroke painted pixels");
    ps::SelectionMask crossing;ps::brushSimilar(halves,crossing,{-1000000,6},{1000000,6},2,18,true,false);
    require(std::all_of(crossing.begin(),crossing.end(),[](uint8_t p){return p==255;}),"Clipped fast stroke left a gap");
    halves.pixels[(6*12+4)*4+3]=0;ps::SelectionMask alpha;
    ps::brushSimilar(halves,alpha,{4,6},{4,6},1,18,true,false);require(!alpha[6*12+4],"Brush selected a fully transparent pixel");
    std::cout<<"PASS: circular similar-color brush, fast interpolation, stroke replay/undo, modifiers, bounds, alpha, adjustable size\n";
}
void testBrushCursor() {
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=40;info.bmiHeader.biHeight=-40;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);throw std::runtime_error("Cannot allocate cursor test canvas");}
    const HGDIOBJ previous=SelectObject(dc,bitmap);fill(dc,{0,0,40,40},RGB(128,128,128));
    App app;app.state=State::Editing;app.wandMode=true;app.selection={4,4,36,36};app.brushRadius=6;
    app.updateBrushCursor({5,5});require(!app.brushHover,"Frame edge did not take priority over the brush cursor");
    app.drag=Drag::Magic;app.updateBrushCursor({5,5});app.paintBrushCursor(dc);GdiFlush();
    size_t dark=0,light=0;bool clipped=true;
    const auto* pixels=static_cast<const uint8_t*>(bits);
    for(int y=0;y<40;++y)for(int x=0;x<40;++x) {
        const uint8_t value=pixels[(y*40+x)*4];
        if(x<4||y<4||x>=36||y>=36)clipped=clipped&&value==128;
        else {if(value==0)++dark;if(value==255)++light;}
    }
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    require(clipped&&dark>2&&light>2,"Circular cursor was missing or painted outside the screenshot region");
    app.hideBrushCursor();require(!app.brushHover,"Mouse leave did not hide brush cursor");
    std::cout<<"PASS: visible circular cursor, screenshot clipping and mouse-leave cleanup\n";
}
void setupFrameTest(App& app) {
    app.state=State::Editing;app.wandMode=true;app.desktop={-100,-50,60,50};
    app.screenshot=ps::Bitmap(160,100);app.selection={20,10,130,90};
    for(int y=0;y<100;++y)for(int x=0;x<160;++x) {
        const bool logo=x>=45&&x<105&&y>=25&&y<75;
        const bool yellow=logo&&x>=75;
        auto* p=app.screenshot.pixels.data()+(y*160+x)*4;
        p[0]=logo?(yellow?60:170):245;p[1]=logo?(yellow?210:180):245;p[2]=logo?(yellow?250:20):245;p[3]=255;
    }
    app.base=ps::crop(app.screenshot,app.selection);app.rendered=app.base;
}
void resizeRight(App& app,int right) {
    const int y=(app.selection.top+app.selection.bottom)/2;
    app.down({app.selection.right,y});require(app.drag==Drag::Resize,"Frame edge did not enter resize mode");
    app.move({right,y});app.up({right,y});
}
void testFrameResizeAndEscape() {
    App app;setupFrameTest(app);app.brushRadius=3;
    app.beginMagicStroke({55,40});app.move({85,40});app.up({85,40});
    require(app.selectedPixels==3000,"Frame fixture did not select the two-color logo");
    const RECT original=app.selection;const auto originalMask=app.magicMask;
    app.wandAction(206);require(app.frameAdjust,"Adjust frame button did not enable frame editing");
    resizeRight(app,145);
    require(app.rendered.width==125&&app.selectedPixels==3000&&app.magicMask==ps::reframeMask(originalMask,original,app.selection),
        "Enlarging frame shifted or discarded brush selection");
    app.down({20,45});app.move({60,45});app.up({60,45});
    require(app.selectedPixels==2250&&app.magicMask==ps::reframeMask(originalMask,original,app.selection),"Shrinking frame did not clip selection at the original pixel positions");
    app.down({60,45});app.move({20,45});app.up({20,45});
    require(app.selectedPixels==3000,"Expanding frame back lost selected pixels outside the temporary crop");
    app.clearMagicSelection();app.selectMagic({30,20});
    const auto background=app.magicMask;const RECT backgroundBounds=app.selection;
    resizeRight(app,155);
    require(app.magicMask==ps::reframeMask(background,backgroundBounds,app.selection),"Expanded frame automatically selected new background pixels");
    const size_t oldCount=app.selectedPixels;
    app.wandAction(206);require(!app.frameAdjust,"Finish adjustment did not restore cutout brush");
    app.selectMagic({150,20});require(app.selectedPixels==oldCount+800,"Could not add newly revealed pixels after frame resize");
    app.undo();require(app.magicMask==ps::reframeMask(background,backgroundBounds,app.selection),"Undo after frame resize lost old selection history");
    app.wandAction(206);const RECT beforeMove=app.selection;const auto beforeMask=app.magicMask;
    app.down({35,20});app.move({40,25});app.up({40,25});
    require(app.selection.left==25&&app.selection.top==15&&app.magicMask==ps::reframeMask(beforeMask,beforeMove,app.selection),
        "Moving frame displaced selected content instead of cropping the same desktop pixels");
    const RECT valid=app.selection;
    resizeRight(app,26);require(EqualRect(&valid,&app.selection),"Invalid tiny resize discarded the prior crop");
    app.keys(VK_ESCAPE);
    require(app.state==State::Idle&&app.screenshot.empty()&&app.wandSeeds.empty()&&app.drag==Drag::None,"Esc did not exit a populated cutout session immediately");
    App active;setupFrameTest(active);active.beginMagicStroke({55,40});active.move({85,40});active.keys(VK_ESCAPE);
    require(active.state==State::Idle&&active.drag==Drag::None&&active.magicMask.empty(),"Esc did not cancel an active brush stroke");
    App resizing;setupFrameTest(resizing);resizing.frameAdjust=true;resizing.down({130,50});resizing.move({145,50});resizing.keys(VK_ESCAPE);
    require(resizing.state==State::Idle&&resizing.screenshot.empty()&&resizing.drag==Drag::None,"Esc did not exit an active frame resize");
    App tiny;tiny.state=State::Editing;tiny.wandMode=true;tiny.frameAdjust=true;tiny.selection={20,10,24,40};
    require((tiny.edgesAt({24,25})&3)==2,"A narrow frame grabbed both opposite edges at once");

    App edited;setupFrameTest(edited);edited.selectMagic({55,40});edited.selectMagic({85,40});edited.applyCutout(true);
    ps::Annotation rectangle;rectangle.tool=ps::Tool::Rectangle;rectangle.points={{32,22},{70,50}};
    edited.addAnnotation(rectangle);edited.refreshDrawing();const auto oldPixels=edited.rendered;
    edited.wandAction(206);resizeRight(edited,150);
    for(int y=0;y<oldPixels.height;++y)for(int x=0;x<oldPixels.width;++x)for(int c=0;c<4;++c)
        require(oldPixels.pixels[(y*oldPixels.width+x)*4+c]==edited.rendered.pixels[(y*edited.rendered.width+x)*4+c],"Resize changed existing annotation or cutout pixels");
    require(edited.rendered.pixels[(10*edited.rendered.width+120)*4+3]==0,"Keep cutout introduced an opaque background when enlarging frame");
    edited.undo();edited.undo();
    require(edited.rendered.pixels[(10*edited.rendered.width+120)*4+3]==255,"Undo after resize did not restore the newly cropped original image");
    edited.selectMagic({55,40});edited.applyCutout(false);resizeRight(edited,155);
    require(edited.rendered.pixels[(10*edited.rendered.width+130)*4+3]==255,"Remove cutout incorrectly hid newly included pixels");
    std::cout<<"PASS: frame resize/move preserves selections, history, annotations and transparency; one-press Esc cancels immediately\n";
}
void renderToolbarTest(App& app) {
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=TOOLBAR_WIDTH;info.bmiHeader.biHeight=-152;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);throw std::runtime_error("Cannot allocate toolbar preview");}
    const HGDIOBJ previous=SelectObject(dc,bitmap);app.paintToolbar(dc);
    const auto paintChild=[&](HWND child,bool button) {
        RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,app.toolbar,reinterpret_cast<POINT*>(&rect),2);
        const int saved=SaveDC(dc);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);IntersectClipRect(dc,0,0,width(rect),height(rect));
        if(button) {
            DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=GetDlgCtrlID(child);item.itemAction=ODA_DRAWENTIRE;
            item.hwndItem=child;item.hDC=dc;item.itemState=IsWindowEnabled(child)?0:ODS_DISABLED;
            GetClientRect(child,&item.rcItem);app.drawButton(item);
        } else SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
        RestoreDC(dc,saved);
    };
    for(auto button:app.toolButtons)paintChild(button,true);
    for(auto button:app.wandButtons)paintChild(button,true);
    paintChild(app.toleranceSlider,false);paintChild(app.brushSlider,false);GdiFlush();
    ps::Bitmap preview(TOOLBAR_WIDTH,152);std::copy_n(static_cast<uint8_t*>(bits),preview.pixels.size(),preview.pixels.begin());
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    for(size_t i=3;i<preview.pixels.size();i+=4)preview.pixels[i]=255;
    const auto output=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(output);
    ps::savePng(preview,output/"toolbar-cutout.png");
}
void testToolbarControls() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    App app;app.instance=GetModuleHandleW(nullptr);app.state=State::Editing;app.wandMode=true;
    WNDCLASSW cls{};cls.hInstance=app.instance;cls.lpfnWndProc=App::toolbarProc;cls.lpszClassName=L"QingTou.TestToolbar";
    require(RegisterClassW(&cls)!=0,"Cannot register hidden toolbar test");
    app.toolbar=CreateWindowExW(0,cls.lpszClassName,L"Hidden toolbar test",WS_POPUP,0,0,TOOLBAR_WIDTH,152,nullptr,nullptr,app.instance,&app);
    require(app.toolbar!=nullptr,"Cannot create hidden toolbar test");app.createToolbarControls();
    require(app.brushSlider&&app.toleranceSlider&&!IsWindowVisible(app.toolbar),"Toolbar test unexpectedly opened on the desktop");
    SendMessageW(app.brushSlider,TBM_SETPOS,TRUE,20);
    SendMessageW(app.toolbar,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,20),reinterpret_cast<LPARAM>(app.brushSlider));
    require(app.brushRadius==20&&app.tolerance==18,"Circle size slider changed tolerance or failed to update brush");
    app.adjustBrush(2);require(SendMessageW(app.brushSlider,TBM_GETPOS,0,0)==22,"Wheel/key size changes did not synchronize slider");
    wchar_t label[32]{};GetWindowTextW(app.toolButtons[5],label,32);require(std::wstring(label)==L"魔棒抠图","Cutout tool label was not updated");
    RECT toolRect{},brushRect{},frameRect{};GetWindowRect(app.toolButtons[5],&toolRect);GetWindowRect(app.brushSlider,&brushRect);GetWindowRect(app.wandButtons[6],&frameRect);
    require(width(toolRect)>=72&&brushRect.right<frameRect.left&&frameRect.bottom<=152,"New toolbar controls overlap or truncate the renamed tool");
    require(!IsWindowEnabled(app.wandButtons[7]),"Tighten button was enabled without selected or transparent content");
    setupFrameTest(app);app.selectMagic({55,40});
    require(IsWindowEnabled(app.wandButtons[7]),"Tighten button did not enable after logo selection");
    renderToolbarTest(app);
    SendMessageW(app.toolbar,WM_COMMAND,MAKEWPARAM(206,BN_CLICKED),reinterpret_cast<LPARAM>(app.wandButtons[6]));
    require(app.frameAdjust,"Adjust frame toolbar button was not wired to its action");
    GetWindowTextW(app.wandButtons[6],label,32);require(std::wstring(label)==L"完成调整","Frame button did not show adjustment state");
    SendMessageW(app.toolbar,WM_COMMAND,MAKEWPARAM(207,BN_CLICKED),reinterpret_cast<LPARAM>(app.wandButtons[7]));
    require(app.rendered.width==30&&app.rendered.height==50&&!app.frameAdjust,"Tighten toolbar button did not crop to the selected color");
    require(!IsWindowEnabled(app.wandButtons[7]),"Already-tight frame still advertised another automatic crop");
    SendMessageW(app.toolbar,WM_KEYDOWN,VK_ESCAPE,0);
    require(app.state==State::Idle&&!app.toolbar&&!app.brushSlider,"Esc from toolbar did not close the capture controls");
    UnregisterClassW(cls.lpszClassName,app.instance);
    std::cout<<"PASS: native circle size slider, synchronization, renamed tool, frame button, Esc from toolbar\n";
}
void setupTrimTest(App& app) {
    setupFrameTest(app);
    for(int y=25;y<75;++y)for(int x=45;x<105;++x) {
        if((x<55||x>=95)&&(y<35||y>=65)) {
            auto* p=app.screenshot.pixels.data()+(y*160+x)*4;p[0]=p[1]=p[2]=245;
        }
    }
    app.base=ps::crop(app.screenshot,app.selection);app.rendered=app.base;app.brushRadius=3;
    app.beginMagicStroke({55,40});app.move({85,40});app.up({85,40});
}
void testTightOutputFrame() {
    App direct;setupFrameTest(direct);direct.selectMagic({55,40});
    require(!direct.frameAdjust,"Direct-resize fixture entered adjustment mode");
    const auto before=direct.magicMask;const RECT old=direct.selection;
    resizeRight(direct,125);
    require(!direct.frameAdjust&&direct.magicMask==ps::reframeMask(before,old,direct.selection),
        "Dragging frame in the active cutout screen required a separate mode or lost the selection");
    direct.down({55,40});require(direct.drag==Drag::Magic,"Frame resizing prevented brush selection inside the crop");direct.keys(VK_ESCAPE);

    App active;setupTrimTest(active);const size_t count=active.selectedPixels;
    active.wandAction(207);const RECT expected{45,25,105,75};
    require(EqualRect(&active.selection,&expected)&&active.rendered.width==60&&active.rendered.height==50&&active.selectedPixels==count,
        "Tightening selected PNG content did not remove the surrounding canvas");
    require(!active.tightenFrame(),"Repeated tightening changed an already-tight frame");
    active.applyCutout(true);
    require(active.rendered.pixels[3]==0&&active.rendered.pixels[(25*60+30)*4+3]>0,"Tight PNG lost interior transparency or visible content");
    const auto output=std::filesystem::current_path()/"test-output";std::filesystem::create_directories(output);
    const auto file=output/"tight-logo.png";ps::savePng(active.rendered,file);
    ps::ComPtr<IWICImagingFactory> factory;
    ps::check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        reinterpret_cast<void**>(factory.put())),"PNG trim test factory");
    ps::ComPtr<IWICBitmapDecoder> decoder;
    ps::check(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.put()),"Read trimmed PNG");
    ps::ComPtr<IWICBitmapFrameDecode> frame;ps::check(decoder->GetFrame(0,frame.put()),"Read trimmed PNG frame");
    UINT w=0,h=0;ps::check(frame->GetSize(&w,&h),"Trimmed PNG size");
    require(w==60&&h==50,"Saved PNG still contains the original large blank canvas");

    App kept;setupTrimTest(kept);kept.applyCutout(true);
    require(!kept.selectedPixels&&kept.tightenFrame()&&kept.rendered.width==60&&kept.rendered.height==50,
        "Could not trim transparent canvas after pressing Keep");
    const RECT alphaBounds=ps::contentBounds(kept.rendered);require(alphaBounds.right==60&&alphaBounds.bottom==50,"Alpha trim clipped a visible edge");
    App empty;setupFrameTest(empty);empty.rendered=ps::Bitmap(110,80);const RECT unchanged=empty.selection;
    require(!empty.tightenFrame()&&EqualRect(&empty.selection,&unchanged),"Empty transparent canvas produced an invalid crop");
    App pixel;setupFrameTest(pixel);pixel.magicMask.assign(110*80,0);pixel.magicMask[5*110+5]=255;pixel.selectedPixels=1;
    pixel.applyCutout(true);require(pixel.tightenFrame()&&pixel.rendered.width==1&&pixel.rendered.height==1,"Single-pixel content retained blank padding");
    std::cout<<"PASS: direct cutout-frame resizing, selected/kept PNG tightening, real PNG dimensions, alpha, empty and one-pixel bounds\n";
}
} // namespace

int main(int argc,char** argv) {
    HWND background=nullptr;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ps::ComApartment apartment;
        if(argc==2&&std::string(argv[1])=="--wand"){testWandInteraction();testAccumulatingLogoSelection();testBrushSelection();testBrushCursor();testFrameResizeAndEscape();testToolbarControls();testTightOutputFrame();return 0;}
        if(argc!=1)throw std::runtime_error("Usage: window_capture_tests [--wand]");
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
        HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSW cls{};cls.lpfnWndProc=backgroundProc;cls.hInstance=instance;cls.lpszClassName=L"QingTou.TestBackground";
        require(RegisterClassW(&cls)!=0,"Cannot register test background");
        const RECT desktop=ps::desktopRect();
        background=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Capture test background",WS_POPUP,
            desktop.left,desktop.top,width(desktop),height(desktop),nullptr,nullptr,instance,nullptr);
        require(background!=nullptr,"Cannot create test background");ShowWindow(background,SW_SHOWNOACTIVATE);
        App app;app.initialize(instance);
        const int x=desktop.left+60,y=desktop.top+60;
        SetWindowPos(app.main,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);app.showMain();
        RECT client{};GetClientRect(app.main,&client);
        POINT origin{0,0};ClientToScreen(app.main,&origin);
        const RECT sample{origin.x+20,origin.y+20,origin.x+client.right-20,origin.y+client.bottom-20};
        composeDesktop();
        require(!showsBackground(ps::capture(sample),{0,0,width(sample),height(sample)},{0,0,width(sample),height(sample)}),
            "Test panel was not present before hiding");
        for(bool gif:{false,true}) {
            app.hideOnCapture=true;app.startSelection(gif);
            app.showMain();
            require(!IsWindowVisible(app.main),"Panel reappeared while preparing capture");
            app.startSelection(!gif); // A second request must not replace the pending capture.
            pumpUntil([&]{return app.state==State::Selecting;});
            require(app.gifSelection==gif,"Pending capture was replaced by another request");
            require(!IsWindowVisible(app.main),"Capture panel is still visible");
            require(showsBackground(app.screenshot,sample,app.desktop),"Hidden panel left pixels in captured image");
            app.showMain();require(!IsWindowVisible(app.main),"Panel reappeared during selection");
            app.cancelSelection();require(IsWindowVisible(app.main),"Cancel did not restore the panel");composeDesktop();
        }
        std::cout<<"PASS: PNG/GIF selection hides panel, captures underlying pixels, blocks reopening, restores on cancel\n";
        app.hideOnCapture=false;app.startSelection(false);
        pumpUntil([&]{return app.state==State::Selecting;});
        require(IsWindowVisible(app.main),"Unchecked option hid the panel");
        require(!showsBackground(app.screenshot,sample,app.desktop),"Unchecked option removed panel from capture");
        app.cancelSelection();
        std::cout<<"PASS: unchecked option preserves panel in captured image\n";
        ShowWindow(app.main,SW_HIDE);app.homeWasVisible=false;app.hideOnCapture=true;app.startSelection(false);
        pumpUntil([&]{return app.state==State::Selecting;});app.cancelSelection();
        require(!IsWindowVisible(app.main),"Cancel unexpectedly reopened a previously hidden panel");
        std::cout<<"PASS: capture from tray keeps panel hidden on cancel\n";
        app.action(ID_QUIT);DestroyWindow(background);background=nullptr;return 0;
    }catch(const std::exception& error) {
        if(background)DestroyWindow(background);
        std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;
    }
}
