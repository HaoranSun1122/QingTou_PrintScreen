#include "theme.h"
#include <dwmapi.h>
namespace ps {
namespace {
COLORREF blend(COLORREF a,COLORREF b,unsigned amount) {
    const auto channel=[&](unsigned x,unsigned y){return (x*amount+y*(100-amount)+50)/100;};
    return RGB(channel(GetRValue(a),GetRValue(b)),channel(GetGValue(a),GetGValue(b)),channel(GetBValue(a),GetBValue(b)));
}
}
UiPalette systemPalette() {
    HIGHCONTRASTW contrast{};contrast.cbSize=sizeof(contrast);
    const bool highContrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)&&(contrast.dwFlags&HCF_HIGHCONTRASTON);
    DWORD light=1,bytes=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
    const bool dark=!light;
    UiPalette p{};p.highContrast=highContrast;p.dark=dark;
    p.focus=GetSysColor(COLOR_HIGHLIGHT);p.selected=p.focus;p.selectedText=GetSysColor(COLOR_HIGHLIGHTTEXT);
    if(highContrast) {
        p.background=GetSysColor(COLOR_WINDOW);p.surface=p.background;p.text=GetSysColor(COLOR_WINDOWTEXT);
        p.muted=p.text;p.border=p.text;p.hover=GetSysColor(COLOR_BTNFACE);
        p.dark=GetRValue(p.background)+GetGValue(p.background)+GetBValue(p.background)<384;
    } else if(dark) {
        p.background=RGB(32,32,32);p.surface=RGB(45,45,45);p.text=RGB(242,242,242);
        p.muted=RGB(185,185,185);p.border=RGB(74,74,74);p.hover=RGB(58,58,58);
    } else {
        p.background=GetSysColor(COLOR_3DFACE);p.surface=GetSysColor(COLOR_WINDOW);p.text=GetSysColor(COLOR_WINDOWTEXT);
        p.muted=blend(p.text,p.surface,62);p.border=blend(p.text,p.surface,20);p.hover=blend(p.focus,p.surface,10);
    }
    return p;
}
void styleWindowFrame(HWND window,const UiPalette& palette) {
    if(!window)return;
    const BOOL dark=palette.dark&&!palette.highContrast;
    // Documented Windows 11 attribute; earlier systems may simply reject it.
    DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
}
} // namespace ps
