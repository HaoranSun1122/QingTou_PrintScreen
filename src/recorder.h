#pragma once
#include "image.h"
#include <atomic>
#include <thread>
namespace ps {
class Recorder {
    std::thread worker_;
    std::atomic<bool> stopping_{false};
public:
    std::string error;
    std::filesystem::path path;
    std::atomic<unsigned> frames{0};
    std::atomic<unsigned> elapsedMs{0};
    ~Recorder();
    void start(RECT area, const std::filesystem::path& output, HWND notify, UINT message);
    void stop();
    void join();
};
} // namespace ps
