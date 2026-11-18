#include "recorder.h"
#include <algorithm>
#include <chrono>
namespace ps {
Recorder::~Recorder() { stop(); join(); }
void Recorder::stop() { stopping_=true; }
void Recorder::join() { if(worker_.joinable()) worker_.join(); }
void Recorder::start(RECT area,const std::filesystem::path& output,HWND notify,UINT message) {
    join(); stopping_=false; frames=0; elapsedMs=0; error.clear(); path=output;
    worker_=std::thread([this,area,notify,message] {
        try {
            ComApartment apartment;
            GifWriter writer(path);
            using clock=std::chrono::steady_clock;
            // Allow the selection overlay to disappear before capturing the live desktop.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto start=clock::now();
            auto previousTime=start;
            Bitmap previous=fitWidth(capture(area,true),1280);
            auto next=start+std::chrono::milliseconds(100);
            while(!stopping_ && clock::now()-start<std::chrono::seconds(60)) {
                std::this_thread::sleep_until(next);
                if(stopping_) break;
                const auto now=clock::now();
                Bitmap current=fitWidth(capture(area,true),1280);
                if(current.pixels!=previous.pixels) {
                    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-previousTime).count();
                    writer.frame(previous,static_cast<unsigned>(std::max<int64_t>(1,(ms+5)/10)));
                    ++frames; previous=std::move(current); previousTime=now;
                }
                elapsedMs=static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-start).count());
                next=std::max(next+std::chrono::milliseconds(100),clock::now());
            }
            const auto finalTime=clock::now();
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(finalTime-previousTime).count();
            writer.frame(previous,static_cast<unsigned>(std::max<int64_t>(1,(ms+5)/10)));
            ++frames; writer.finish();
        } catch(const std::exception& exception) { error=exception.what(); }
        PostMessageW(notify,message,0,0);
    });
}
} // namespace ps
