#pragma once
#include <windows.h>
namespace ps {
struct UiPalette {
    COLORREF background,surface,text,muted,border,hover,selected,selectedText,focus;
    bool dark=false,highContrast=false;
};
UiPalette systemPalette();
void styleWindowFrame(HWND window,const UiPalette& palette);
} // namespace ps
