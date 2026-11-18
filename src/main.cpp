#include "image.h"
#include "recorder.h"
#include "selection.h"
#include "theme.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr UINT WM_TRAY=WM_APP+1, WM_RECORDED=WM_APP+2, WM_REQUEST=WM_APP+3;
constexpr UINT_PTR TIMER_CAPTURE=1;
constexpr UINT_PTR TIMER_WAND=1;
const UINT WM_TASKBAR_RECREATED=RegisterWindowMessageW(L"TaskbarCreated");
constexpr int ID_PNG=10,ID_GIF=11,ID_FOLDER=12,ID_KEYS=13,ID_QUIT=14,ID_PANEL=15,ID_STOP=16,ID_HIDE_ON_CAPTURE=17;
constexpr std::array<uint32_t,4> COLORS={0xFF535D,0xFFD166,0x36D1DC,0xFFFFFF};
constexpr std::array<const wchar_t*,12> TOOL_LABELS={L"矩形",L"箭头",L"画笔",L"文字",L"马赛克",L"魔棒抠图",L"颜色",L"撤销",L"GIF",L"保存",L"复制",L"取消"};
constexpr int TOOLBAR_WIDTH=616;
constexpr std::array<ps::Tool,5> TOOLS={ps::Tool::Rectangle,ps::Tool::Arrow,ps::Tool::Pen,ps::Tool::Text,ps::Tool::Mosaic};
int width(RECT r) {return static_cast<int>(r.right-r.left);}
int height(RECT r) {return static_cast<int>(r.bottom-r.top);}
bool contains(RECT r,POINT p) {return PtInRect(&r,p)!=FALSE;}
void fill(HDC dc,RECT r,COLORREF color) {HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void text(HDC dc,const std::wstring& value,RECT r,int size,COLORREF color,bool bold=false,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);
    LOGFONTW description{};
    if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0))description=metrics.lfMessageFont;
    else wcscpy_s(description.lfFaceName,L"Segoe UI");
    description.lfHeight=-size;description.lfWeight=bold?FW_SEMIBOLD:FW_NORMAL;description.lfQuality=CLEARTYPE_QUALITY;
    HFONT font=CreateFontIndirectW(&description);
    HGDIOBJ old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&r,flags);
    SelectObject(dc,old);DeleteObject(font);
}
void outline(HDC dc,RECT r,COLORREF color,int thickness=1) {
    HPEN pen=CreatePen(PS_SOLID,thickness,color);HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
}
std::wstring wide(const std::string& value) {
    if(value.empty()) return {};
    int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(count,0);MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count);return result;
}
std::wstring filename(const wchar_t* extension) {
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t buffer[120];
    swprintf(buffer,120,L"Screen_%04d%02d%02d_%02d%02d%02d_%03d.%ls",now.wYear,now.wMonth,now.wDay,
        now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,extension);return buffer;
}
UINT hotkeyModifiers(WORD key) {
    const BYTE mod=HIBYTE(key);UINT result=MOD_NOREPEAT;
    if(mod&HOTKEYF_CONTROL)result|=MOD_CONTROL;
    if(mod&HOTKEYF_ALT)result|=MOD_ALT;
    if(mod&HOTKEYF_SHIFT)result|=MOD_SHIFT;
    return result;
}
std::wstring hotkeyText(WORD key) {
    std::wstring result;BYTE mod=HIBYTE(key);
    if(mod&HOTKEYF_CONTROL)result+=L"Ctrl + ";
    if(mod&HOTKEYF_ALT)result+=L"Alt + ";
    if(mod&HOTKEYF_SHIFT)result+=L"Shift + ";
    wchar_t name[80]{};const UINT scan=MapVirtualKeyW(LOBYTE(key),MAPVK_VK_TO_VSC);
    GetKeyNameTextW(static_cast<LONG>(scan<<16),name,80);return result+name;
}

enum class State {Idle,Preparing,Selecting,Editing,Recording,Finalizing};
enum class Drag {None,Select,Move,Resize,Draw,Magic};
struct App {
    HINSTANCE instance=nullptr;
    HWND main=nullptr,overlay=nullptr,toolbar=nullptr,recordBar=nullptr;
    HICON icon=nullptr,smallIcon=nullptr,headerIcon=nullptr;
    ps::UiPalette colors=ps::systemPalette();
    std::filesystem::path directory,config;
    WORD pngKey=MAKEWORD('Q',HOTKEYF_CONTROL|HOTKEYF_ALT),gifKey=MAKEWORD('G',HOTKEYF_CONTROL|HOTKEYF_ALT);
    std::wstring status=L"准备好了。按快捷键，或点击下面的按钮。";
    State state=State::Idle;
    Drag drag=Drag::None;
    RECT desktop{},selection{},dragRect{};
    POINT anchor{};
    int resizeEdges=0,color=0;
    bool gifSelection=false,homeWasVisible=false,hasActive=false,exiting=false,hideOnCapture=true;
    bool captureHidesHome=false,restoreTransitions=false;
    BOOL previousTransitions=FALSE;
    ps::Tool tool=ps::Tool::None;
    ps::Bitmap screenshot,shade,base,rendered,preview;
    struct CanvasAnnotation {ps::Annotation value;RECT bounds;};
    std::vector<CanvasAnnotation> annotations;
    ps::Annotation active;
    ps::Recorder recorder;
    NOTIFYICONDATAW tray{};
    std::array<HWND,12> toolButtons{};
    std::array<HWND,8> wandButtons{};
    HWND toleranceSlider=nullptr,brushSlider=nullptr;
    bool wandMode=false,contiguous=true,soften=true,wandUpdatePending=false,frameAdjust=false;
    int tolerance=18;
    ps::SelectionMask magicMask;
    size_t selectedPixels=0;
    struct WandSeed {POINT point;int mode=0;std::vector<POINT> path;int radius=0;std::shared_ptr<const ps::Bitmap> source;RECT bounds;}; // 0 add, 1 subtract, 2 invert
    std::vector<WandSeed> wandSeeds;
    std::shared_ptr<const ps::Bitmap> wandReference;
    int brushRadius=5,strokeRadius=5,brushWheel=0;
    POINT brushCursor{},strokeStart{},strokeLast{};
    bool brushHover=false,brushTracking=false,strokeSubtract=false,strokeOwnsAction=false,strokeReplace=false;
    size_t strokeBeforeCount=0;
    std::wstring dialogText;

    static App& from(HWND window) {return *reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
    static bool bind(HWND window,UINT message,LPARAM parameter) {
        if(message==WM_NCCREATE) {SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams));return true;}
        return GetWindowLongPtrW(window,GWLP_USERDATA)!=0;
    }
    int scale(int value) const {return MulDiv(value,static_cast<int>(GetDpiForWindow(main)),96);}
    void refreshTheme() {
        colors=ps::systemPalette();
        for(HWND window:{main,overlay,toolbar,recordBar})if(window){
            ps::styleWindowFrame(window,colors);RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        }
    }
    void error(const std::string& value) {MessageBoxW(main,(L"操作未完成：\n"+wide(value)).c_str(),L"轻透截图",MB_OK|MB_ICONERROR);}
    void balloon(const std::wstring& title,const std::wstring& message) {
        tray.uFlags=NIF_INFO;tray.dwInfoFlags=NIIF_INFO|NIIF_NOSOUND;
        wcsncpy_s(tray.szInfoTitle,title.c_str(),_TRUNCATE);wcsncpy_s(tray.szInfo,message.c_str(),_TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY,&tray);
    }
    void showMain() {
        if(captureHidesHome&&state!=State::Idle)return;
        ShowWindow(main,SW_RESTORE);SetForegroundWindow(main);InvalidateRect(main,nullptr,FALSE);
    }
    void openFolder() {std::filesystem::create_directories(directory);ShellExecuteW(main,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
    bool registerKeys(WORD png,WORD gif) {
        if(!LOBYTE(png)||!LOBYTE(gif)||png==gif) return false;
        UnregisterHotKey(main,1);UnregisterHotKey(main,2);
        const bool a=RegisterHotKey(main,1,hotkeyModifiers(png),LOBYTE(png));
        const bool b=RegisterHotKey(main,2,hotkeyModifiers(gif),LOBYTE(gif));
        if(a&&b) return true;
        UnregisterHotKey(main,1);UnregisterHotKey(main,2);
        RegisterHotKey(main,1,hotkeyModifiers(pngKey),LOBYTE(pngKey));
        RegisterHotKey(main,2,hotkeyModifiers(gifKey),LOBYTE(gifKey));return false;
    }
    void settings() {if(state==State::Idle)DialogBoxParamW(instance,MAKEINTRESOURCEW(102),main,settingsDialog,reinterpret_cast<LPARAM>(this));}
    static INT_PTR CALLBACK settingsDialog(HWND dialog,UINT message,WPARAM w,LPARAM l) {
        if(message==WM_INITDIALOG) {
            auto* app=reinterpret_cast<App*>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);
            SendDlgItemMessageW(dialog,1002,HKM_SETHOTKEY,app->pngKey,0);
            SendDlgItemMessageW(dialog,1003,HKM_SETHOTKEY,app->gifKey,0);return TRUE;
        }
        if(message==WM_COMMAND&&LOWORD(w)==IDOK) {
            auto& app=*reinterpret_cast<App*>(GetWindowLongPtrW(dialog,DWLP_USER));
            WORD png=static_cast<WORD>(SendDlgItemMessageW(dialog,1002,HKM_GETHOTKEY,0,0));
            WORD gif=static_cast<WORD>(SendDlgItemMessageW(dialog,1003,HKM_GETHOTKEY,0,0));
            if(!(HIBYTE(png)&(HOTKEYF_CONTROL|HOTKEYF_ALT))||!(HIBYTE(gif)&(HOTKEYF_CONTROL|HOTKEYF_ALT))||!app.registerKeys(png,gif)) {
                MessageBoxW(dialog,L"请设置两个不同的快捷键，包含 Ctrl 或 Alt。\n其中一个组合可能已被其他程序占用。",L"快捷键不可用",MB_OK|MB_ICONINFORMATION);return TRUE;
            }
            app.pngKey=png;app.gifKey=gif;
            WritePrivateProfileStringW(L"Keys",L"PNG",std::to_wstring(png).c_str(),app.config.c_str());
            WritePrivateProfileStringW(L"Keys",L"GIF",std::to_wstring(gif).c_str(),app.config.c_str());
            app.status=L"快捷键已保存。";InvalidateRect(app.main,nullptr,FALSE);EndDialog(dialog,IDOK);return TRUE;
        }
        if(message==WM_COMMAND&&LOWORD(w)==IDCANCEL) {EndDialog(dialog,IDCANCEL);return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK textDialog(HWND dialog,UINT message,WPARAM w,LPARAM l) {
        if(message==WM_INITDIALOG) {
            SetWindowLongPtrW(dialog,DWLP_USER,l);SendDlgItemMessageW(dialog,1001,EM_SETLIMITTEXT,2000,0);
            SetFocus(GetDlgItem(dialog,1001));return FALSE;
        }
        if(message==WM_COMMAND&&LOWORD(w)==IDOK) {
            auto& app=*reinterpret_cast<App*>(GetWindowLongPtrW(dialog,DWLP_USER));
            int length=GetWindowTextLengthW(GetDlgItem(dialog,1001));
            std::wstring value(static_cast<size_t>(length)+1,L'\0');
            GetDlgItemTextW(dialog,1001,value.data(),length+1);value.resize(length);app.dialogText=value;
            EndDialog(dialog,IDOK);return TRUE;
        }
        if(message==WM_COMMAND&&LOWORD(w)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}return FALSE;
    }
    void layoutHome() {
        const int s=scale(28),gap=scale(12),y=scale(125),buttonWidth=scale(250);
        MoveWindow(GetDlgItem(main,ID_PNG),s,y,buttonWidth,scale(100),TRUE);
        MoveWindow(GetDlgItem(main,ID_GIF),s+buttonWidth+gap,y,buttonWidth,scale(100),TRUE);
        MoveWindow(GetDlgItem(main,ID_HIDE_ON_CAPTURE),s,scale(230),buttonWidth,scale(24),TRUE);
        MoveWindow(GetDlgItem(main,ID_FOLDER),s,scale(264),scale(140),scale(36),TRUE);
        MoveWindow(GetDlgItem(main,ID_KEYS),s+scale(152),scale(264),scale(112),scale(36),TRUE);
        MoveWindow(GetDlgItem(main,ID_QUIT),s+scale(442),scale(264),scale(70),scale(36),TRUE);
    }
    static LRESULT CALLBACK hideOptionProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
        if(message==WM_PAINT){
            auto& app=*reinterpret_cast<App*>(data);
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);
            fill(dc,client,app.colors.background);
            const int size=app.scale(17),top=(height(client)-size)/2;
            RECT box{0,top,size,top+size};
            UINT state=DFCS_BUTTONCHECK;
            if(SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED)state|=DFCS_CHECKED;
            if(SendMessageW(window,BM_GETSTATE,0,0)&BST_PUSHED)state|=DFCS_PUSHED;
            DrawFrameControl(dc,&box,DFC_BUTTON,state);
            RECT label{size+app.scale(7),0,client.right,client.bottom};
            text(dc,L"截图时隐藏窗口",label,app.scale(13),app.colors.text);
            if(GetFocus()==window){InflateRect(&label,-1,-1);DrawFocusRect(dc,&label);}
            EndPaint(window,&paint);return 0;
        }
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_NCDESTROY)RemoveWindowSubclass(window,hideOptionProc,id);
        return DefSubclassProc(window,message,w,l);
    }
    void paintHome(HDC dc) {
        RECT client{};GetClientRect(main,&client);fill(dc,client,colors.background);
        DrawIconEx(dc,scale(28),scale(22),headerIcon,scale(64),scale(64),0,nullptr,DI_NORMAL);
        text(dc,L"轻透截图",{scale(108),scale(24),scale(540),scale(65)},scale(27),colors.text,true);
        text(dc,L"截图 · 透明 PNG · GIF 录制",{scale(108),scale(69),scale(540),scale(96)},scale(14),colors.muted);
        text(dc,L"框选后可标注，或用「魔棒抠图」提取画面。",{scale(28),scale(99),scale(540),scale(119)},scale(13),colors.muted);
        text(dc,L"GIF：10 帧/秒 · 最长 60 秒 · 宽度最多 1280 像素",{scale(28),scale(318),scale(550),scale(342)},scale(12),colors.muted);
        text(dc,status,{scale(28),scale(352),scale(540),scale(381)},scale(12),colors.text,false,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        text(dc,L"关闭面板后留在托盘；右键托盘图标可退出。",{scale(28),scale(390),scale(540),scale(414)},scale(12),colors.muted);
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        bool big=item.CtlID==ID_PNG||item.CtlID==ID_GIF;
        const bool homeButton=GetParent(item.hwndItem)==main;
        COLORREF background=colors.surface;
        bool selected=false;
        if(item.CtlID>=100&&item.CtlID<112) {
            int index=item.CtlID-100;
            selected=(index<5&&!wandMode&&tool==TOOLS[index])||(index==5&&wandMode);
            background=selected?colors.selected:colors.surface;
        }
        if((item.CtlID==200&&contiguous)||(item.CtlID==201&&soften)||(item.CtlID==206&&frameAdjust))selected=true;
        if(selected)background=colors.selected;
        if(item.itemState&ODS_SELECTED)background=colors.hover;
        fill(item.hDC,item.rcItem,background);
        if(homeButton)outline(item.hDC,item.rcItem,colors.border);
        RECT r=item.rcItem;
        if(big) {
            r.left+=scale(18);r.top+=scale(14);r.bottom=r.top+scale(32);
            const bool gif=item.CtlID==ID_GIF;
            text(item.hDC,gif?L"录制 GIF":L"截图 / 抠图",r,scale(21),colors.text,true);
            r.top+=scale(36);r.bottom=r.top+scale(23);
            text(item.hDC,hotkeyText(gif?gifKey:pngKey),r,scale(13),colors.muted);
        } else {
            wchar_t label[100]{};GetWindowTextW(item.hwndItem,label,100);
            const bool accent=item.CtlID==108||item.CtlID==110||item.CtlID==ID_STOP||item.CtlID==202||item.CtlID==203||item.CtlID==207;
            const COLORREF ink=(item.itemState&ODS_DISABLED)?colors.muted:(selected&&!(item.itemState&ODS_SELECTED))?colors.selectedText:colors.text;
            text(item.hDC,label,r,homeButton?scale(13):14,ink,accent,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(item.CtlID==106) {
                RECT line{r.left+12,r.bottom-5,r.right-12,r.bottom-2};uint32_t c=COLORS[color];
                fill(item.hDC,line,RGB((c>>16)&255,(c>>8)&255,c&255));
            }
        }
        if(item.itemState&ODS_FOCUS) {RECT r2=item.rcItem;InflateRect(&r2,-2,-2);outline(item.hDC,r2,colors.focus);}
    }
    void startSelection(bool gif) {
        if(state!=State::Idle)return;
        homeWasVisible=IsWindowVisible(main);
        captureHidesHome=hideOnCapture;gifSelection=gif;state=State::Preparing;
        if(captureHidesHome) {
            if(SUCCEEDED(DwmGetWindowAttribute(main,DWMWA_TRANSITIONS_FORCEDISABLED,&previousTransitions,sizeof(previousTransitions)))) {
                const BOOL disabled=TRUE;
                restoreTransitions=SUCCEEDED(DwmSetWindowAttribute(main,DWMWA_TRANSITIONS_FORCEDISABLED,&disabled,sizeof(disabled)));
            }
            ShowWindow(main,SW_HIDE);
            if(IsWindowVisible(main))throw std::runtime_error("Cannot hide capture panel");
        }
        // Return to the message loop so activation, repainting and desktop
        // composition can finish before BitBlt reads the screen.
        if(!SetTimer(main,TIMER_CAPTURE,captureHidesHome?200:1,nullptr))
            throw std::runtime_error("Cannot schedule screen capture");
    }
    void restoreCaptureTransitions() {
        if(restoreTransitions) {
            DwmSetWindowAttribute(main,DWMWA_TRANSITIONS_FORCEDISABLED,&previousTransitions,sizeof(previousTransitions));
            restoreTransitions=false;
        }
    }
    void beginSelection() {
        KillTimer(main,TIMER_CAPTURE);
        if(state!=State::Preparing)return;
        if(captureHidesHome)ShowWindow(main,SW_HIDE);
        DwmFlush();
        desktop=ps::desktopRect();
        screenshot=ps::capture(desktop);shade=ps::dimmed(screenshot);
        restoreCaptureTransitions();
        selection={};annotations.clear();base={};rendered={};preview={};tool=ps::Tool::None;drag=Drag::None;
        wandMode=false;frameAdjust=false;clearMagicSelection();
        state=State::Selecting;
        overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"QingTou.Overlay",L"轻透截图 · 框选",WS_POPUP,
            desktop.left,desktop.top,width(desktop),height(desktop),main,nullptr,instance,this);
        if(!overlay)throw std::runtime_error("Cannot create selection window");
        ShowWindow(overlay,SW_SHOW);SetForegroundWindow(overlay);SetFocus(overlay);UpdateWindow(overlay);
    }
    void releaseSelection() {
        KillTimer(main,TIMER_CAPTURE);restoreCaptureTransitions();
        drag=Drag::None;if(overlay&&GetCapture()==overlay)ReleaseCapture();
        if(toolbar) {DestroyWindow(toolbar);toolbar=nullptr;}if(overlay) {DestroyWindow(overlay);overlay=nullptr;}
        toleranceSlider=brushSlider=nullptr;wandButtons.fill(nullptr);toolButtons.fill(nullptr);
        screenshot={};shade={};base={};rendered={};preview={};annotations.clear();hasActive=false;drag=Drag::None;
        clearMagicSelection();wandMode=false;frameAdjust=false;brushHover=false;brushTracking=false;brushWheel=0;
    }
    void cancelSelection() {
        releaseSelection();state=State::Idle;if(homeWasVisible)showMain();
    }
    POINT point(LPARAM l)const {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        p.x=std::clamp<LONG>(p.x,0,width(desktop));p.y=std::clamp<LONG>(p.y,0,height(desktop));return p;
    }
    POINT local(POINT p)const {return {std::clamp<LONG>(p.x-selection.left,0,std::max(0,width(selection)-1)),
        std::clamp<LONG>(p.y-selection.top,0,std::max(0,height(selection)-1))};}
    void refreshDrawing() {
        if(base.empty())return;
        std::vector<ps::Annotation> items;items.reserve(annotations.size()+1);
        for(const auto& stored:annotations) {
            auto item=stored.value;
            for(auto& point:item.points){point.x+=stored.bounds.left-selection.left;point.y+=stored.bounds.top-selection.top;}
            if(item.tool==ps::Tool::Cutout)item.opacity=ps::reframeMask(item.opacity,stored.bounds,selection,item.outsideOpacity);
            items.push_back(std::move(item));
        }
        if(hasActive)items.push_back(active);
        rendered=ps::annotate(base,items);
        if(!wandSeeds.empty())recomputeMagicSelection();else refreshPreview();
    }
    void addAnnotation(ps::Annotation item) {annotations.push_back({std::move(item),selection});}
    void refreshPreview() {
        if(rendered.empty())return;
        if(!wandMode)hideBrushCursor();
        preview=ps::transparencyPreview(rendered,magicMask);
        updateTightButton();
        if(overlay)InvalidateRect(overlay,nullptr,FALSE);
        if(toolbar)InvalidateRect(toolbar,nullptr,FALSE);
    }
    void clearMagicSelection() {
        if(drag==Drag::Magic){drag=Drag::None;if(overlay&&GetCapture()==overlay)ReleaseCapture();}
        strokeOwnsAction=false;
        if(toolbar)KillTimer(toolbar,TIMER_WAND);
        wandUpdatePending=false;
        magicMask=ps::SelectionMask();wandSeeds.clear();wandReference.reset();selectedPixels=0;
    }
    void ensureWandReference() {if(!wandReference)wandReference=std::make_shared<const ps::Bitmap>(rendered);}
    void recomputeMagicSelection() {
        if(toolbar)KillTimer(toolbar,TIMER_WAND);
        wandUpdatePending=false;
        if(rendered.empty())return;
        RECT canvas=selection;
        for(const auto& seed:wandSeeds){RECT expanded{};UnionRect(&expanded,&canvas,&seed.bounds);canvas=expanded;}
        ps::SelectionMask accumulated(static_cast<size_t>(width(canvas))*height(canvas),0);
        for(const auto& seed:wandSeeds) {
            auto localMask=ps::reframeMask(accumulated,canvas,seed.bounds);
            if(seed.mode==2)ps::invertSelection(localMask,*seed.source);
            else {
                ps::combineSelection(localMask,ps::selectSimilar(*seed.source,seed.point,tolerance,contiguous),seed.mode==1);
                for(size_t i=1;i<seed.path.size();++i)
                    ps::brushSimilar(*seed.source,localMask,seed.path[i-1],seed.path[i],seed.radius,tolerance,contiguous,seed.mode==1);
            }
            for(int y=0;y<height(seed.bounds);++y)
                std::copy_n(localMask.begin()+static_cast<size_t>(y)*width(seed.bounds),width(seed.bounds),
                    accumulated.begin()+static_cast<size_t>(y+seed.bounds.top-canvas.top)*width(canvas)+seed.bounds.left-canvas.left);
        }
        magicMask=ps::reframeMask(accumulated,canvas,selection);
        selectedPixels=static_cast<size_t>(std::count_if(magicMask.begin(),magicMask.end(),[](uint8_t value){return value!=0;}));refreshPreview();
    }
    bool selectMagic(POINT p,bool subtract=false,bool add=true) {
        if(!subtract&&!add)clearMagicSelection();
        ensureWandReference();
        const POINT seed=local(p);const int mode=subtract?1:0;
        const bool append=wandSeeds.empty()||wandSeeds.back().source!=wandReference||!wandSeeds.back().path.empty()||wandSeeds.back().mode!=mode||wandSeeds.back().point.x!=seed.x||wandSeeds.back().point.y!=seed.y;
        if(append)wandSeeds.push_back({seed,mode,{},0,wandReference,selection});
        recomputeMagicSelection();return append;
    }
    void beginMagicStroke(POINT p,bool subtract=false,bool add=true) {
        if(wandUpdatePending)recomputeMagicSelection();
        const size_t before=selectedPixels;
        const bool owns=selectMagic(p,subtract,add);
        strokeBeforeCount=before;strokeOwnsAction=owns;strokeSubtract=subtract;strokeReplace=!subtract&&!add;
        strokeRadius=brushRadius;strokeStart=strokeLast=local(p);drag=Drag::Magic;
    }
    void extendMagicStroke(POINT p) {
        const POINT next{p.x-selection.left,p.y-selection.top};
        if(next.x==strokeLast.x&&next.y==strokeLast.y)return;
        if(!strokeOwnsAction) {
            wandSeeds.push_back({strokeStart,strokeSubtract?1:0,{},0,wandReference,selection});strokeOwnsAction=true;
        }
        auto& action=wandSeeds.back();
        if(action.path.empty()){action.path.push_back(strokeStart);action.radius=strokeRadius;}
        action.path.push_back(next);
        const bool changed=ps::brushSimilar(rendered,magicMask,strokeLast,next,strokeRadius,tolerance,contiguous,strokeSubtract);
        strokeLast=next;
        if(changed){selectedPixels=static_cast<size_t>(std::count_if(magicMask.begin(),magicMask.end(),[](uint8_t v){return v!=0;}));refreshPreview();}
    }
    void finishMagicStroke() {
        if(drag!=Drag::Magic)return;
        drag=Drag::None;
        if(strokeOwnsAction&&!strokeReplace&&selectedPixels==strokeBeforeCount)wandSeeds.pop_back();
        strokeOwnsAction=false;
        if(overlay&&GetCapture()==overlay)ReleaseCapture();
    }
    void invalidateBrushCursor() {
        if(!overlay||!brushHover)return;
        RECT r{brushCursor.x-brushRadius-3,brushCursor.y-brushRadius-3,brushCursor.x+brushRadius+4,brushCursor.y+brushRadius+4};
        InvalidateRect(overlay,&r,FALSE);
    }
    void hideBrushCursor() {invalidateBrushCursor();brushHover=false;}
    void updateBrushCursor(POINT p) {
        invalidateBrushCursor();brushCursor=p;
        brushHover=state==State::Editing&&wandMode&&!frameAdjust&&drag!=Drag::Resize&&drag!=Drag::Move&&edgesAt(p)==0&&contains(selection,p);
        if(overlay)SetCursor(cursorForPoint(p));
        if(brushHover&&overlay&&!brushTracking) {
            TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,overlay,0};brushTracking=TrackMouseEvent(&tracking)!=FALSE;
        }
        invalidateBrushCursor();
    }
    void setBrushRadius(int radius) {
        if(drag==Drag::Magic)return;
        invalidateBrushCursor();brushRadius=std::clamp(radius,1,64);invalidateBrushCursor();
        if(brushSlider)SendMessageW(brushSlider,TBM_SETPOS,TRUE,brushRadius);
        if(toolbar)InvalidateRect(toolbar,nullptr,FALSE);
    }
    void adjustBrush(int delta) {setBrushRadius(brushRadius+delta);}
    void paintBrushCursor(HDC dc) {
        if(!wandMode||!brushHover||state!=State::Editing)return;
        const int saved=SaveDC(dc);IntersectClipRect(dc,selection.left,selection.top,selection.right,selection.bottom);
        HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        const int x=brushCursor.x,y=brushCursor.y,r=brushRadius;
        HPEN dark=CreatePen(PS_SOLID,3,RGB(0,0,0)),light=CreatePen(PS_SOLID,1,RGB(255,255,255));
        HGDIOBJ oldPen=SelectObject(dc,dark);Ellipse(dc,x-r,y-r,x+r+1,y+r+1);
        SelectObject(dc,light);Ellipse(dc,x-r,y-r,x+r+1,y+r+1);
        MoveToEx(dc,x-2,y,nullptr);LineTo(dc,x+3,y);
        if(!(drag==Drag::Magic?strokeSubtract:(GetKeyState(VK_MENU)&0x8000)!=0)){MoveToEx(dc,x,y-2,nullptr);LineTo(dc,x,y+3);}
        SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(dark);DeleteObject(light);RestoreDC(dc,saved);
    }
    void applyCutout(bool keep) {
        finishFrameDrag();
        finishMagicStroke();
        if(wandUpdatePending)recomputeMagicSelection();
        if(!selectedPixels)return;
        ps::Annotation operation;operation.tool=ps::Tool::Cutout;
        operation.opacity=ps::cutoutOpacity(magicMask,rendered.width,rendered.height,keep,soften);
        operation.outsideOpacity=keep?0:255;
        addAnnotation(std::move(operation));clearMagicSelection();refreshDrawing();
    }
    void wandAction(int id) {
        finishFrameDrag();
        finishMagicStroke();
        if(id==200){contiguous=!contiguous;SetWindowTextW(wandButtons[0],contiguous?L"连续区域":L"全图相似");recomputeMagicSelection();}
        if(id==201){soften=!soften;SetWindowTextW(wandButtons[1],soften?L"柔边 ✓":L"柔边");}
        if(id==202)applyCutout(false);
        if(id==203)applyCutout(true);
        if(id==204){ensureWandReference();wandSeeds.push_back({{},2,{},0,wandReference,selection});recomputeMagicSelection();}
        if(id==205){clearMagicSelection();refreshPreview();}
        if(id==206){frameAdjust=!frameAdjust;hideBrushCursor();SetWindowTextW(wandButtons[6],frameAdjust?L"完成调整":L"调整截取框");if(overlay)InvalidateRect(overlay,nullptr,FALSE);}
        if(id==207)tightenFrame();
        for(auto button:wandButtons)if(button)InvalidateRect(button,nullptr,FALSE);
        if(overlay)SetFocus(overlay);
    }
    int edgesAt(POINT p)const {
        if(state!=State::Editing||tool!=ps::Tool::None||drag==Drag::Magic)return 0;
        RECT expanded=selection;InflateRect(&expanded,7,7);if(!contains(expanded,p))return 0;
        int edges=0;
        const LONG left=std::abs(p.x-selection.left),right=std::abs(p.x-selection.right);
        const LONG top=std::abs(p.y-selection.top),bottom=std::abs(p.y-selection.bottom);
        if(std::min(left,right)<=7)edges|=left<=right?1:2;
        if(std::min(top,bottom)<=7)edges|=top<=bottom?4:8;
        return edges;
    }
    HCURSOR cursorForPoint(POINT p)const {
        const int edges=edgesAt(p);
        if(edges==5||edges==10)return LoadCursorW(nullptr,IDC_SIZENWSE);
        if(edges==6||edges==9)return LoadCursorW(nullptr,IDC_SIZENESW);
        if(edges&3)return LoadCursorW(nullptr,IDC_SIZEWE);
        if(edges&12)return LoadCursorW(nullptr,IDC_SIZENS);
        if(frameAdjust&&contains(selection,p))return LoadCursorW(nullptr,IDC_SIZEALL);
        return brushHover?nullptr:LoadCursorW(nullptr,IDC_CROSS);
    }
    void changeFrame(RECT target) {
        selection=target;base=ps::crop(screenshot,selection);wandReference.reset();refreshDrawing();
        if(overlay){showToolbar();InvalidateRect(overlay,nullptr,FALSE);}
    }
    RECT tightBounds()const {
        return selectedPixels?ps::contentBounds(rendered,magicMask):ps::contentBounds(rendered);
    }
    void updateTightButton() {
        if(!wandButtons[7])return;
        const RECT bounds=tightBounds();
        const bool tighter=!IsRectEmpty(&bounds)&&(bounds.left||bounds.top||bounds.right!=rendered.width||bounds.bottom!=rendered.height);
        EnableWindow(wandButtons[7],tighter);
    }
    bool tightenFrame() {
        finishFrameDrag();finishMagicStroke();
        if(wandUpdatePending)recomputeMagicSelection();
        if(state!=State::Editing||rendered.empty())return false;
        RECT bounds=tightBounds();
        if(IsRectEmpty(&bounds)||(bounds.left==0&&bounds.top==0&&bounds.right==rendered.width&&bounds.bottom==rendered.height))return false;
        OffsetRect(&bounds,selection.left,selection.top);
        frameAdjust=false;hideBrushCursor();
        if(wandButtons[6])SetWindowTextW(wandButtons[6],L"调整截取框");
        changeFrame(bounds);return true;
    }
    void finishFrameDrag() {
        if(drag!=Drag::Resize&&drag!=Drag::Move)return;
        drag=Drag::None;
        if(overlay&&GetCapture()==overlay)ReleaseCapture();
        if(width(selection)<4||height(selection)<4)selection=dragRect;
        changeFrame(selection);
    }
    void down(POINT p) {
        if(state!=State::Selecting&&state!=State::Editing)return;
        const int edges=edgesAt(p);
        if(state==State::Editing&&wandMode&&!frameAdjust&&!edges&&contains(selection,p)){
            beginMagicStroke(p,(GetKeyState(VK_MENU)&0x8000)!=0,(GetKeyState(VK_CONTROL)&0x8000)==0);
            if(overlay)SetCapture(overlay);
            return;
        }
        if(state==State::Editing&&tool==ps::Tool::Text&&contains(selection,p)) {
            dialogText.clear();
            if(DialogBoxParamW(instance,MAKEINTRESOURCEW(101),overlay,textDialog,reinterpret_cast<LPARAM>(this))==IDOK&&!dialogText.empty()) {
                ps::Annotation a;a.tool=tool;a.points.push_back(local(p));a.text=dialogText;a.color=COLORS[color];addAnnotation(std::move(a));refreshDrawing();
            }
            SetFocus(overlay);return;
        }
        anchor=p;dragRect=selection;
        resizeEdges=edges;
        if(resizeEdges)drag=Drag::Resize;
        else if(state==State::Editing&&contains(selection,p)&&tool!=ps::Tool::None) {
            drag=Drag::Draw;active={};active.tool=tool;active.color=COLORS[color];active.points={local(p)};
            if(tool!=ps::Tool::Pen)active.points.push_back(local(p));
            hasActive=true;
        } else if(state==State::Editing&&contains(selection,p)&&(frameAdjust||annotations.empty()))drag=Drag::Move;
        else if(state==State::Editing&&contains(selection,p))return;
        else {drag=Drag::Select;state=State::Selecting;selection={p.x,p.y,p.x,p.y};base={};rendered={};preview={};annotations.clear();tool=ps::Tool::None;clearMagicSelection();wandMode=false;frameAdjust=false;}
        if(toolbar)ShowWindow(toolbar,SW_HIDE);
        if(overlay)SetCapture(overlay);
    }
    void move(POINT p) {
        updateBrushCursor(p);
        if(drag==Drag::None)return;
        if(drag==Drag::Magic){extendMagicStroke(p);return;}
        if(drag==Drag::Select)selection=ps::normalized(anchor,p);
        else if(drag==Drag::Move) {
            int dx=std::clamp<int>(p.x-anchor.x,-dragRect.left,width(desktop)-dragRect.right);
            int dy=std::clamp<int>(p.y-anchor.y,-dragRect.top,height(desktop)-dragRect.bottom);
            selection=dragRect;OffsetRect(&selection,dx,dy);
        } else if(drag==Drag::Resize) {
            RECT r=dragRect;if(resizeEdges&1)r.left=p.x;if(resizeEdges&2)r.right=p.x;
            if(resizeEdges&4)r.top=p.y;
            if(resizeEdges&8)r.bottom=p.y;
            selection=ps::normalized({r.left,r.top},{r.right,r.bottom});
        } else if(drag==Drag::Draw) {
            POINT lp=local(p);
            if(tool==ps::Tool::Pen) {
                POINT previous=active.points.back();if(std::abs(lp.x-previous.x)+std::abs(lp.y-previous.y)>=2)active.points.push_back(lp);
            } else active.points.back()=lp;
            refreshDrawing();return;
        }
        if(overlay)InvalidateRect(overlay,nullptr,FALSE);
    }
    void up(POINT p) {
        if(drag==Drag::None)return;
        if(drag==Drag::Magic){move(p);finishMagicStroke();return;}
        if(drag==Drag::Resize||drag==Drag::Move){move(p);finishFrameDrag();return;}
        move(p);Drag previous=drag;drag=Drag::None;ReleaseCapture();
        if(previous==Drag::Draw) {
            if(hasActive)addAnnotation(std::move(active));
            hasActive=false;refreshDrawing();
        } else if(width(selection)>=4&&height(selection)>=4) {
            state=State::Editing;base=ps::crop(screenshot,selection);rendered=base;refreshPreview();
            if(gifSelection) {startRecording();return;}
        } else {state=State::Selecting;selection={};return;}
        showToolbar();InvalidateRect(overlay,nullptr,FALSE);
    }
    void paintOverlay(HDC dc) {
        ps::drawBitmap(dc,shade,0,0);
        if(width(selection)>0&&height(selection)>0) {
            if(drag==Drag::Resize||drag==Drag::Move) {
                const int saved=SaveDC(dc);IntersectClipRect(dc,selection.left,selection.top,selection.right,selection.bottom);
                ps::drawBitmap(dc,screenshot,0,0);ps::drawBitmap(dc,preview,dragRect.left,dragRect.top);RestoreDC(dc,saved);
            }
            else if(state==State::Editing&&!preview.empty())ps::drawBitmap(dc,preview,selection.left,selection.top);
            else {int saved=SaveDC(dc);IntersectClipRect(dc,selection.left,selection.top,selection.right,selection.bottom);ps::drawBitmap(dc,screenshot,0,0);RestoreDC(dc,saved);}
            outline(dc,selection,colors.focus,2);
            if(tool==ps::Tool::None) {
                for(int x:{static_cast<int>(selection.left),static_cast<int>((selection.left+selection.right)/2),static_cast<int>(selection.right)})
                    for(int y:{static_cast<int>(selection.top),static_cast<int>((selection.top+selection.bottom)/2),static_cast<int>(selection.bottom)}) {
                        if(x==(selection.left+selection.right)/2&&y==(selection.top+selection.bottom)/2)continue;
                        fill(dc,{x-3,y-3,x+4,y+4},colors.focus);
                    }
            }
            const int labelY=std::max(3,static_cast<int>(selection.top)-31);
            RECT label{selection.left,labelY,selection.left+210,labelY+25};
            fill(dc,label,colors.background);label.left+=8;
            text(dc,std::to_wstring(width(selection))+L" × "+std::to_wstring(height(selection))+L" px",label,13,colors.text);
        }
        if(state==State::Selecting) {
            RECT label{std::max(10,width(desktop)/2-240),32,std::max(10,width(desktop)/2-240)+480,74};fill(dc,label,colors.background);
            text(dc,gifSelection?L"拖动框选录制区域 · 松开后开始 · Esc 取消":L"拖动框选 · 标注后 Enter 复制 · Ctrl+S 保存 · Esc 取消",label,14,colors.text,false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        paintBrushCursor(dc);
    }
    void paintToolbar(HDC dc) {
        RECT r{};GetClientRect(toolbar,&r);fill(dc,r,colors.surface);
        if(!wandMode)return;
        text(dc,L"容差 "+std::to_wstring(tolerance),{12,51,76,83},12,colors.text);
        text(dc,L"圆圈 "+std::to_wstring(brushRadius*2)+L"px",{12,88,80,120},12,colors.text);
        text(dc,L"拖边框缩放",{508,88,604,120},12,colors.muted);
        const std::wstring hint=(selectedPixels?L"已选 "+std::to_wstring(selectedPixels)+L" 像素 · ":L"")+
            (frameAdjust?L"框内拖动可移动范围 · Esc 退出截图":L"边框可直接缩放 · 滚轮 / [ ] 调圆圈 · Alt 减选 · Esc 退出");
        text(dc,hint,{12,124,604,146},12,colors.muted,false,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    void createToolbarControls() {
            int x=8;
            for(size_t i=0;i<TOOL_LABELS.size();++i) {
                const int buttonWidth=i==5?72:48;
                toolButtons[i]=CreateWindowExW(0,L"BUTTON",TOOL_LABELS[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                    x,6,buttonWidth,36,toolbar,reinterpret_cast<HMENU>(100+i),instance,nullptr);
                x+=buttonWidth;
            }
            toleranceSlider=CreateWindowExW(0,TRACKBAR_CLASSW,L"相似颜色容差",WS_CHILD|TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,
                76,51,104,32,toolbar,reinterpret_cast<HMENU>(210),instance,nullptr);
            SendMessageW(toleranceSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(toleranceSlider,TBM_SETPOS,TRUE,tolerance);
            const std::array<const wchar_t*,6> labels={L"连续区域",L"柔边",L"移除",L"保留",L"反选",L"清除"};
            for(int i=0;i<6;++i)wandButtons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,
                188+i*66,51,60,32,toolbar,reinterpret_cast<HMENU>(static_cast<INT_PTR>(200+i)),instance,nullptr);
            brushSlider=CreateWindowExW(0,TRACKBAR_CLASSW,L"圆圈大小",WS_CHILD|TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,
                80,88,164,32,toolbar,reinterpret_cast<HMENU>(211),instance,nullptr);
            SendMessageW(brushSlider,TBM_SETRANGE,TRUE,MAKELPARAM(1,64));SendMessageW(brushSlider,TBM_SETPOS,TRUE,brushRadius);
            wandButtons[6]=CreateWindowExW(0,L"BUTTON",L"调整截取框",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,
                260,88,112,32,toolbar,reinterpret_cast<HMENU>(206),instance,nullptr);
            wandButtons[7]=CreateWindowExW(0,L"BUTTON",L"收紧到选区",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,
                384,88,112,32,toolbar,reinterpret_cast<HMENU>(207),instance,nullptr);
            updateTightButton();
    }
    void showToolbar() {
        if(!toolbar) {
            toolbar=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"QingTou.Toolbar",L"轻透截图 · 工具栏",WS_POPUP,
                0,0,TOOLBAR_WIDTH,48,overlay,nullptr,instance,this);
            if(!toolbar)throw std::runtime_error("Cannot create annotation toolbar");
            createToolbarControls();
        }
        ShowWindow(toleranceSlider,wandMode?SW_SHOW:SW_HIDE);
        ShowWindow(brushSlider,wandMode?SW_SHOW:SW_HIDE);
        for(auto button:wandButtons)ShowWindow(button,wandMode?SW_SHOW:SW_HIDE);
        SetWindowTextW(wandButtons[0],contiguous?L"连续区域":L"全图相似");SetWindowTextW(wandButtons[1],soften?L"柔边 ✓":L"柔边");
        SetWindowTextW(wandButtons[6],frameAdjust?L"完成调整":L"调整截取框");
        updateTightButton();
        const int barHeight=wandMode?152:48;
        POINT screen{desktop.left+selection.left,desktop.top+selection.bottom};
        HMONITOR monitor=MonitorFromPoint(screen,MONITOR_DEFAULTTONEAREST);MONITORINFO info{};info.cbSize=sizeof(info);GetMonitorInfoW(monitor,&info);
        int x=std::clamp<int>(screen.x,info.rcMonitor.left,std::max<LONG>(info.rcMonitor.left,info.rcMonitor.right-TOOLBAR_WIDTH));
        int y=screen.y+12;if(y+barHeight>info.rcMonitor.bottom)y=desktop.top+selection.top-barHeight-12;
        y=std::clamp<int>(y,info.rcMonitor.top,std::max<LONG>(info.rcMonitor.top,info.rcMonitor.bottom-barHeight));
        SetWindowPos(toolbar,HWND_TOPMOST,x,y,TOOLBAR_WIDTH,barHeight,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(toolbar,nullptr,TRUE);
    }
    void toolbarAction(int index) {
        if(state!=State::Editing)return;
        finishFrameDrag();finishMagicStroke();
        if(index<0||index>=static_cast<int>(TOOL_LABELS.size()))return;
        if(index<5){tool=tool==TOOLS[index]?ps::Tool::None:TOOLS[index];wandMode=false;frameAdjust=false;clearMagicSelection();refreshPreview();}
        else if(index==5){wandMode=!wandMode;frameAdjust=false;tool=ps::Tool::None;if(!wandMode){clearMagicSelection();refreshPreview();}}
        else if(index==6)color=(color+1)%static_cast<int>(COLORS.size());
        else if(index==7)undo();
        else if(index==8) {startRecording();return;}
        else if(index==9) {saveScreenshot();return;}
        else if(index==10) {copyScreenshot();return;}
        else if(index==11) {cancelSelection();return;}
        showToolbar();
        if(toolbar)for(auto button:toolButtons)InvalidateRect(button,nullptr,FALSE);
        if(overlay)SetFocus(overlay);
    }
    void undo() {
        finishFrameDrag();
        finishMagicStroke();
        if(!wandSeeds.empty()){wandSeeds.pop_back();recomputeMagicSelection();return;}
        if(!annotations.empty()){annotations.pop_back();clearMagicSelection();refreshDrawing();}
    }
    void keys(WPARAM key) {
        if(key==VK_ESCAPE){cancelSelection();return;}
        if(wandMode&&(key==VK_OEM_4||key==VK_OEM_6)){adjustBrush(key==VK_OEM_4?-1:1);return;}
        if(key==VK_DELETE||key==VK_RETURN||(GetKeyState(VK_CONTROL)&0x8000)){finishFrameDrag();finishMagicStroke();}
        if(state!=State::Editing)return;
        if(key==VK_DELETE&&wandMode)applyCutout(false);
        else if(key==VK_RETURN)copyScreenshot();
        else if(GetKeyState(VK_CONTROL)&0x8000) {
            if(key=='S')saveScreenshot();else if(key=='Z')undo();else if(key=='C')copyScreenshot();
        } else {
            if(key=='R')toolbarAction(0);
            if(key=='A')toolbarAction(1);
            if(key=='P')toolbarAction(2);
            if(key=='T')toolbarAction(3);
            if(key=='M')toolbarAction(4);
            if(key=='W')toolbarAction(5);
            if(key=='G')toolbarAction(8);
        }
    }
    void copyScreenshot() {
        finishFrameDrag();
        if(rendered.empty())return;
        if(wandUpdatePending)recomputeMagicSelection();
        if(selectedPixels)applyCutout(true);
        ps::copyImage(main,rendered);
        releaseSelection();state=State::Idle;status=L"截图已复制，可直接粘贴。";
        balloon(L"截图已复制",L"可以粘贴到聊天、文档或图片编辑器。");
    }
    void saveScreenshot() {
        finishFrameDrag();
        if(rendered.empty())return;
        if(wandUpdatePending)recomputeMagicSelection();
        if(selectedPixels)applyCutout(true);
        std::filesystem::create_directories(directory);
        wchar_t path[32768]{};const auto initial=filename(L"png");wcsncpy_s(path,initial.c_str(),_TRUNCATE);
        OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=overlay;
        dialog.lpstrFilter=L"PNG 图片 (*.png)\0*.png\0\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;
        dialog.lpstrInitialDir=directory.c_str();dialog.lpstrDefExt=L"png";dialog.lpstrTitle=L"保存截图";
        dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(GetSaveFileNameW(&dialog)) {
            ps::savePng(rendered,path);const std::filesystem::path savedFile=path;releaseSelection();state=State::Idle;
            status=L"已保存："+savedFile.filename().wstring();balloon(L"PNG 已保存",savedFile.wstring());
        }
        if(overlay)SetFocus(overlay);
    }
    void startRecording() {
        RECT area=selection;OffsetRect(&area,desktop.left,desktop.top);
        std::filesystem::create_directories(directory);auto output=directory/filename(L"gif");
        releaseSelection();state=State::Recording;
        try {
        recordBar=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"QingTou.Record",L"轻透截图 · 正在录制 GIF",WS_POPUP,
            0,0,400,72,main,nullptr,instance,this);
        if(!recordBar)throw std::runtime_error("Cannot create recording controls");
        CreateWindowExW(0,L"BUTTON",L"停止并保存",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            272,14,114,44,recordBar,reinterpret_cast<HMENU>(ID_STOP),instance,nullptr);
        MONITORINFO info{};info.cbSize=sizeof(info);GetMonitorInfoW(MonitorFromRect(&area,MONITOR_DEFAULTTONEAREST),&info);
        int y=area.bottom+12;if(y+72>info.rcWork.bottom)y=area.top-84;
        if(y<info.rcWork.top)y=info.rcWork.top+16;
        int x=std::clamp<int>(area.left,info.rcWork.left,std::max<LONG>(info.rcWork.left,info.rcWork.right-400));
        // Windows 10 2004+ omits this overlay from supported screen-capture paths.
        SetWindowDisplayAffinity(recordBar,0x00000011);
        SetWindowPos(recordBar,HWND_TOPMOST,x,y,400,72,SWP_SHOWWINDOW|SWP_NOACTIVATE);DwmFlush();
        recorder.start(area,output,main,WM_RECORDED);SetTimer(recordBar,1,250,nullptr);
        } catch(...) {
            recorder.stop();recorder.join();
            if(recordBar){DestroyWindow(recordBar);recordBar=nullptr;}
            state=State::Idle;showMain();throw;
        }
    }
    void stopRecording() {
        if(state!=State::Recording)return;
        state=State::Finalizing;recorder.stop();
        if(recordBar){EnableWindow(GetDlgItem(recordBar,ID_STOP),FALSE);InvalidateRect(recordBar,nullptr,FALSE);}
    }
    void recorded() {
        recorder.join();if(recordBar){DestroyWindow(recordBar);recordBar=nullptr;}state=State::Idle;
        if(!recorder.error.empty()) {status=L"录制失败："+wide(recorder.error);error(recorder.error);showMain();return;}
        status=L"GIF 已保存："+recorder.path.filename().wstring();
        try {ps::copyFile(main,recorder.path);balloon(L"GIF 已保存，文件已复制",L"可粘贴到支持文件粘贴的软件。\n"+recorder.path.wstring());}
        catch(const std::exception&){balloon(L"GIF 已保存",L"剪贴板被占用，请从保存目录中取用文件。\n"+recorder.path.wstring());}
        InvalidateRect(main,nullptr,FALSE);
    }
    void trayMenu() {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,ID_PANEL,L"打开面板");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,ID_PNG,L"截图 PNG");AppendMenuW(menu,MF_STRING,ID_GIF,state==State::Recording?L"停止 GIF 录制":L"录制 GIF");
        AppendMenuW(menu,MF_STRING,ID_FOLDER,L"打开保存目录");AppendMenuW(menu,MF_STRING,ID_KEYS,L"快捷键");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,ID_QUIT,L"退出");
        POINT p{};GetCursorPos(&p);SetForegroundWindow(main);
        UINT command=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,p.x,p.y,0,main,nullptr);DestroyMenu(menu);
        PostMessageW(main,WM_NULL,0,0);if(command)action(command);
    }
    void action(int id) {
        switch(id) {
        case ID_PANEL:showMain();break;
        case ID_PNG:startSelection(false);break;
        case ID_GIF:if(state==State::Recording)stopRecording();else startSelection(true);break;
        case ID_FOLDER:openFolder();break;
        case ID_KEYS:settings();break;
        case ID_HIDE_ON_CAPTURE:
            hideOnCapture=SendDlgItemMessageW(main,ID_HIDE_ON_CAPTURE,BM_GETCHECK,0,0)==BST_CHECKED;
            if(!WritePrivateProfileStringW(L"Capture",L"HideWindowOnCapture",hideOnCapture?L"1":L"0",config.c_str()))
                throw std::runtime_error("Cannot save capture setting");
            break;
        case ID_QUIT:exiting=true;recorder.stop();recorder.join();releaseSelection();if(recordBar)DestroyWindow(recordBar);DestroyWindow(main);break;
        }
    }
    static LRESULT CALLBACK mainProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        if(!bind(window,message,l))return DefWindowProcW(window,message,w,l);
        auto& app=from(window);
        try {
            if(message==WM_SETTINGCHANGE||message==WM_THEMECHANGED||message==WM_SYSCOLORCHANGE||message==WM_DWMCOLORIZATIONCOLORCHANGED){app.refreshTheme();return 0;}
            if(message==WM_TASKBAR_RECREATED){app.tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;Shell_NotifyIconW(NIM_ADD,&app.tray);return 0;}
            if(message==WM_CREATE) {
                app.main=window;
                for(auto id:{ID_PNG,ID_GIF,ID_FOLDER,ID_KEYS,ID_QUIT}) {
                    const wchar_t* label=id==ID_PNG?L"截图 PNG":id==ID_GIF?L"录制 GIF":id==ID_FOLDER?L"打开保存目录":id==ID_KEYS?L"快捷键":L"退出";
                    CreateWindowExW(0,L"BUTTON",label,WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr);
                }
                HWND option=CreateWindowExW(0,L"BUTTON",L"截图时隐藏窗口",WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX,
                    0,0,0,0,window,reinterpret_cast<HMENU>(ID_HIDE_ON_CAPTURE),app.instance,nullptr);
                if(!option||!SetWindowSubclass(option,hideOptionProc,1,reinterpret_cast<DWORD_PTR>(&app)))
                    throw std::runtime_error("Cannot create capture option");
                SendMessageW(option,BM_SETCHECK,app.hideOnCapture?BST_CHECKED:BST_UNCHECKED,0);
                app.layoutHome();return 0;
            }
            if(message==WM_COMMAND){
                if(LOWORD(w)==IDCANCEL){if(app.state==State::Idle)ShowWindow(window,SW_HIDE);else app.keys(VK_ESCAPE);}
                else app.action(LOWORD(w));
                return 0;
            }
            if(message==WM_TIMER&&w==TIMER_CAPTURE){app.beginSelection();return 0;}
            if(message==WM_HOTKEY){if(w==2&&app.state==State::Recording)app.stopRecording();else if(app.state==State::Idle)app.startSelection(w==2);return 0;}
            if(message==WM_REQUEST){if(w==1)app.startSelection(false);else if(w==2)app.startSelection(true);else app.showMain();return 0;}
            if(message==WM_RECORDED){if(!app.exiting)app.recorded();return 0;}
            if(message==WM_DRAWITEM){app.drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
            if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);app.paintHome(dc);EndPaint(window,&paint);return 0;}
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_DPICHANGED){auto r=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,width(*r),height(*r),SWP_NOZORDER|SWP_NOACTIVATE);app.layoutHome();return 0;}
            if(message==WM_TRAY) {
                if(l==WM_LBUTTONUP||l==WM_LBUTTONDBLCLK)app.showMain();
                if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU)app.trayMenu();
                return 0;
            }
            if(message==WM_CLOSE){ShowWindow(window,SW_HIDE);return 0;}
            if(message==WM_QUERYENDSESSION){app.recorder.stop();return TRUE;}
            if(message==WM_DESTROY){UnregisterHotKey(window,1);UnregisterHotKey(window,2);Shell_NotifyIconW(NIM_DELETE,&app.tray);PostQuitMessage(0);return 0;}
        } catch(const std::exception& exception) {
            if(app.state==State::Preparing||app.state==State::Selecting||app.state==State::Editing)app.cancelSelection();
            app.error(exception.what());
        }
        return DefWindowProcW(window,message,w,l);
    }
    static LRESULT CALLBACK overlayProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        if(!bind(window,message,l))return DefWindowProcW(window,message,w,l);
        auto& app=from(window);
        try {
            if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);app.paintOverlay(dc);EndPaint(window,&paint);return 0;}
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_LBUTTONDOWN){app.down(app.point(l));return 0;}
            if(message==WM_MOUSEMOVE){app.move(app.point(l));return 0;}
            if(message==WM_MOUSELEAVE){app.hideBrushCursor();app.brushTracking=false;return 0;}
            if(message==WM_MOUSEWHEEL&&app.wandMode){
                app.brushWheel+=GET_WHEEL_DELTA_WPARAM(w);
                const int steps=app.brushWheel/WHEEL_DELTA;app.brushWheel%=WHEEL_DELTA;
                if(steps)app.adjustBrush(steps);
                return 0;
            }
            if(message==WM_CAPTURECHANGED&&app.drag==Drag::Magic){app.finishMagicStroke();return 0;}
            if(message==WM_CAPTURECHANGED&&(app.drag==Drag::Resize||app.drag==Drag::Move)){app.finishFrameDrag();return 0;}
            if(message==WM_LBUTTONUP){app.up(app.point(l));return 0;}
            if(message==WM_LBUTTONDBLCLK&&app.state==State::Editing&&app.wandMode){app.down(app.point(l));return 0;}
            if(message==WM_LBUTTONDBLCLK&&app.state==State::Editing&&app.tool==ps::Tool::None&&!app.wandMode){app.copyScreenshot();return 0;}
            if(message==WM_RBUTTONUP){app.cancelSelection();return 0;}
            if(message==WM_KEYDOWN){app.keys(w);return 0;}
            if((message==WM_SYSKEYDOWN||message==WM_SYSKEYUP)&&w==VK_MENU){app.invalidateBrushCursor();return 0;}
            if(message==WM_SETCURSOR){SetCursor(app.cursorForPoint(app.brushCursor));return TRUE;}
            if(message==WM_DISPLAYCHANGE){app.cancelSelection();return 0;}
            if(message==WM_CLOSE){app.cancelSelection();return 0;}
        } catch(const std::exception& exception){app.error(exception.what());}
        return DefWindowProcW(window,message,w,l);
    }
    static LRESULT CALLBACK toolbarProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        if(!bind(window,message,l))return DefWindowProcW(window,message,w,l);
        auto& app=from(window);
        try {
            if(message==WM_COMMAND){if(LOWORD(w)>=200&&LOWORD(w)<208)app.wandAction(LOWORD(w));else app.toolbarAction(LOWORD(w)-100);return 0;}
            if(message==WM_HSCROLL&&reinterpret_cast<HWND>(l)==app.brushSlider){
                app.finishMagicStroke();app.setBrushRadius(static_cast<int>(SendMessageW(app.brushSlider,TBM_GETPOS,0,0)));return 0;
            }
            if(message==WM_HSCROLL&&reinterpret_cast<HWND>(l)==app.toleranceSlider){
                app.tolerance=static_cast<int>(SendMessageW(app.toleranceSlider,TBM_GETPOS,0,0));
                InvalidateRect(window,nullptr,FALSE);
                if(LOWORD(w)==TB_ENDTRACK||!app.wandUpdatePending) {
                    if(LOWORD(w)==TB_ENDTRACK)app.recomputeMagicSelection();
                    else {app.wandUpdatePending=SetTimer(window,TIMER_WAND,50,nullptr)!=0;if(!app.wandUpdatePending)app.recomputeMagicSelection();}
                }
                return 0;
            }
            if(message==WM_TIMER&&w==TIMER_WAND){app.recomputeMagicSelection();return 0;}
            if(message==WM_PAINT){
                PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);app.paintToolbar(dc);
                EndPaint(window,&paint);return 0;
            }
            if(message==WM_CTLCOLORSTATIC){SetTextColor(reinterpret_cast<HDC>(w),app.colors.text);SetBkColor(reinterpret_cast<HDC>(w),app.colors.surface);SetDCBrushColor(reinterpret_cast<HDC>(w),app.colors.surface);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
            if(message==WM_DRAWITEM){app.drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
            if(message==WM_ERASEBKGND){RECT r{};GetClientRect(window,&r);fill(reinterpret_cast<HDC>(w),r,app.colors.surface);return TRUE;}
            if(message==WM_KEYDOWN){app.keys(w);return 0;}
        }catch(const std::exception& exception){app.error(exception.what());}return DefWindowProcW(window,message,w,l);
    }
    static LRESULT CALLBACK recordProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        if(!bind(window,message,l))return DefWindowProcW(window,message,w,l);
        auto& app=from(window);
        if(message==WM_COMMAND&&LOWORD(w)==ID_STOP){app.stopRecording();return 0;}
        if(message==WM_TIMER){InvalidateRect(window,nullptr,FALSE);return 0;}
        if(message==WM_DRAWITEM){app.drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(message==WM_CLOSE){app.stopRecording();return 0;}
        if(message==WM_KEYDOWN&&w==VK_ESCAPE){app.stopRecording();return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT) {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);fill(dc,r,app.colors.background);outline(dc,r,app.colors.border);
            const unsigned seconds=app.recorder.elapsedMs/1000;
            std::wstring title=app.state==State::Finalizing?L"正在保存 GIF…":L"● GIF  "+std::to_wstring(seconds)+L"s / 60s";
            text(dc,title,{16,10,265,37},17,app.colors.text,true);
            text(dc,hotkeyText(app.gifKey)+L" 停止",{16,39,267,63},12,app.colors.muted);EndPaint(window,&paint);return 0;
        }
        return DefWindowProcW(window,message,w,l);
    }
    void initialize(HINSTANCE h) {
        instance=h;wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto root=std::filesystem::path(executable).parent_path();directory=root/L"captures";config=root/L"QingTou.ini";
        pngKey=static_cast<WORD>(GetPrivateProfileIntW(L"Keys",L"PNG",pngKey,config.c_str()));
        gifKey=static_cast<WORD>(GetPrivateProfileIntW(L"Keys",L"GIF",gifKey,config.c_str()));
        hideOnCapture=GetPrivateProfileIntW(L"Capture",L"HideWindowOnCapture",1,config.c_str())!=0;
        icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR));
        smallIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));
        headerIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,128,128,LR_DEFAULTCOLOR));
        if(!icon||!smallIcon||!headerIcon)throw std::runtime_error("Cannot load app icons");
        const std::array<std::pair<const wchar_t*,WNDPROC>,4> classes={{{L"QingTou.Main",mainProc},{L"QingTou.Overlay",overlayProc},{L"QingTou.Toolbar",toolbarProc},{L"QingTou.Record",recordProc}}};
        for(const auto& [name,procedure]:classes) {
            WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.style=CS_DBLCLKS;cls.lpfnWndProc=procedure;cls.hInstance=instance;
            cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=icon;cls.hIconSm=smallIcon;cls.lpszClassName=name;
            if(!RegisterClassExW(&cls))throw std::runtime_error("Cannot register window class");
        }
        UINT dpi=GetDpiForSystem();RECT size{0,0,MulDiv(568,dpi,96),MulDiv(430,dpi,96)};
        AdjustWindowRectExForDpi(&size,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0,dpi);
        main=CreateWindowExW(0,L"QingTou.Main",L"轻透截图",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,width(size),height(size),nullptr,nullptr,instance,this);
        if(!main)throw std::runtime_error("Cannot create main window");
        ps::styleWindowFrame(main,colors);
        tray.cbSize=sizeof(tray);tray.hWnd=main;tray.uID=1;tray.uCallbackMessage=WM_TRAY;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.hIcon=smallIcon;
        wcsncpy_s(tray.szTip,L"轻透截图 · 透明 PNG 与 GIF",_TRUNCATE);
        if(!Shell_NotifyIconW(NIM_ADD,&tray))throw std::runtime_error("Cannot create tray icon");
        if(!registerKeys(pngKey,gifKey))status=L"快捷键被占用，请点击「快捷键」更换组合。";
    }
    ~App(){recorder.stop();recorder.join();if(icon)DestroyIcon(icon);if(smallIcon)DestroyIcon(smallIcon);if(headerIcon)DestroyIcon(headerIcon);}
};
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);int request=0;bool trayOnly=false;
    for(int i=1;i<count;++i){
        const std::wstring argument=args[i];
        if(argument==L"--capture")request=1;
        if(argument==L"--gif")request=2;
        if(argument==L"--tray")trayOnly=true;
    }
    if(args)LocalFree(args);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\QingTou.Desktop");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(L"QingTou.Main",nullptr);if(existing)PostMessageW(existing,WM_REQUEST,request,0);CloseHandle(mutex);return 0;
    }
    int result=0;
    try {
        ps::ComApartment apartment;INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
        App app;app.initialize(instance);
        if(request)PostMessageW(app.main,WM_REQUEST,request,0);else if(!trayOnly)app.showMain();
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0) {
            if(message.message==WM_KEYDOWN&&app.toolbar&&(message.hwnd==app.toolbar||IsChild(app.toolbar,message.hwnd))&&
                (message.wParam==VK_ESCAPE||message.wParam==VK_RETURN||message.wParam==VK_DELETE||
                 (app.wandMode&&(message.wParam==VK_OEM_4||message.wParam==VK_OEM_6))||(GetKeyState(VK_CONTROL)&0x8000))) {
                try{app.keys(message.wParam);}catch(const std::exception& exception){app.error(exception.what());}
                continue;
            }
            const bool homeMessage=message.hwnd==app.main||IsChild(app.main,message.hwnd);
            if(!homeMessage||!IsDialogMessageW(app.main,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
        }
        result=static_cast<int>(message.wParam);
    }catch(const std::exception& exception){MessageBoxW(nullptr,wide(exception.what()).c_str(),L"轻透截图 启动失败",MB_OK|MB_ICONERROR);result=1;}
    CloseHandle(mutex);
    return result;
}
