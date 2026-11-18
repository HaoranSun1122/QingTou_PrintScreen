#pragma once
#include <windows.h>
#include <wincodec.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps {
template<class T> class ComPtr {
    T* ptr_ = nullptr;
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T** put() { reset(); return &ptr_; }
    void reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }
};
void check(HRESULT hr, const char* operation);
struct ComApartment {
    HRESULT result;
    ComApartment() : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) { check(result, "COM initialization"); }
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Bitmap {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels; // Top-down, straight-alpha BGRA, physical pixels.
    Bitmap() = default;
    Bitmap(int w, int h);
    bool empty() const { return pixels.empty(); }
};
RECT desktopRect();
RECT normalized(POINT a, POINT b);
Bitmap capture(RECT screenRect, bool cursor = false);
Bitmap crop(const Bitmap& source, RECT area);
Bitmap fitWidth(const Bitmap& source, int maxWidth);
Bitmap dimmed(const Bitmap& source);
void drawBitmap(HDC dc, const Bitmap& bitmap, int x, int y);
enum class Tool { None, Rectangle, Arrow, Pen, Text, Mosaic, Cutout };
struct Annotation {
    Tool tool = Tool::None;
    std::vector<POINT> points;
    std::wstring text;
    uint32_t color = 0xFF535D; // RGB
    float thickness = 3;
    std::vector<uint8_t> opacity; // Cutout operation: multiplier for each pixel's alpha.
    uint8_t outsideOpacity = 255; // Crop expansion after remove (255) or keep (0).
};
Bitmap annotate(const Bitmap& base, const std::vector<Annotation>& annotations);
std::vector<uint8_t> pngBytes(const Bitmap& bitmap);
void savePng(const Bitmap& bitmap, const std::filesystem::path& path);
void copyImage(HWND owner, const Bitmap& bitmap);
void copyFile(HWND owner, const std::filesystem::path& path);

class GifWriter {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit GifWriter(const std::filesystem::path& destination);
    ~GifWriter();
    void frame(const Bitmap& bitmap, unsigned delayCentiseconds);
    void finish();
};
} // namespace ps
