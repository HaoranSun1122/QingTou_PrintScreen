#include "selection.h"
#include <algorithm>
#include <iostream>
#include <random>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
size_t selectedCount(const ps::SelectionMask& mask) {
    return static_cast<size_t>(std::count_if(mask.begin(),mask.end(),[](uint8_t value){return value!=0;}));
}
}

void testSelectionSessions() {
    ps::Bitmap image(79,53);std::mt19937 random(70819);
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        const size_t i=static_cast<size_t>(y)*image.width+x;auto* pixel=image.pixels.data()+i*4;
        const int gray=210+(x/9+y/7)%4*9;
        pixel[0]=static_cast<uint8_t>(gray);pixel[1]=static_cast<uint8_t>(gray+1);pixel[2]=static_cast<uint8_t>(gray+2);
        pixel[3]=i%29==0?0:static_cast<uint8_t>(70+i%186);
    }
    const auto original=image.pixels;
    for(bool connected:{false,true})for(int tolerance:{0,1,18,100}) {
        ps::SimilarSelection session(image,tolerance,connected);
        ps::SelectionMask actual(image.width*image.height,0),expected=actual;
        auto preview=ps::transparencyPreview(image,actual);
        const auto* previewStorage=preview.pixels.data();
        for(int action=0;action<20;++action) {
            POINT from{static_cast<LONG>(random()%image.width),static_cast<LONG>(random()%image.height)};
            POINT to{static_cast<LONG>(random()%image.width),static_cast<LONG>(random()%image.height)};
            const bool subtract=action%3==2;const auto before=actual;
            ps::SelectionChange change;
            if(action%2) {
                change=session.brush(actual,from,to,2,subtract);
                ps::brushSimilar(image,expected,from,to,2,tolerance,connected,subtract);
            } else {
                change=session.select(actual,from,subtract);
                ps::combineSelection(expected,ps::selectSimilar(image,from,tolerance,connected),subtract);
            }
            require(actual==expected,"Cached edges or visited spans leaked between selection operations");
            require(change.selectedDelta==static_cast<int64_t>(selectedCount(actual))-static_cast<int64_t>(selectedCount(before)),
                "Incremental selected-pixel count is incorrect");
            size_t changed=0;
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
                const size_t i=static_cast<size_t>(y)*image.width+x;
                if(actual[i]==before[i])continue;
                ++changed;POINT point{x,y};require(PtInRect(&change.bounds,point)!=FALSE,"Changed bounds missed a modified pixel");
            }
            require(changed==change.pixels,"Changed-pixel count includes untouched pixels");
            if(change.pixels)ps::updateTransparencyPreview(preview,image,actual,change.bounds);
            require(preview.pixels==ps::transparencyPreview(image,actual).pixels,"Partial preview left stale tint or selection outlines");
            require(preview.pixels.data()==previewStorage,"Partial preview reallocated the full image");
        }
    }
    require(image.pixels==original,"Interactive selection or preview modified export pixels");

    // Adding next to an existing selection must repaint its former outline,
    // but must leave the rest of a large preview untouched.
    ps::SelectionMask mask(image.width*image.height,0);mask[20*image.width+20]=255;
    auto preview=ps::transparencyPreview(image,mask);const auto before=preview.pixels;
    mask[20*image.width+21]=255;const RECT dirty=ps::updateTransparencyPreview(preview,image,mask,{21,20,22,21});
    const RECT expectedDirty{20,19,23,22};require(EqualRect(&dirty,&expectedDirty)!=FALSE,"Partial preview did not limit the one-pixel outline halo");
    require(preview.pixels==ps::transparencyPreview(image,mask).pixels,"Adjacent addition left an internal marching-ants edge");
    require(std::equal(preview.pixels.begin(),preview.pixels.begin()+image.width*4,before.begin()),"Partial preview touched a distant row");
    mask[20*image.width+21]=0;ps::updateTransparencyPreview(preview,image,mask,{21,20,22,21});
    require(preview.pixels==ps::transparencyPreview(image,mask).pixels,"Subtraction did not restore a neighboring outline");
    ps::Bitmap missing;ps::updateTransparencyPreview(missing,image,mask,{});
    require(missing.pixels==preview.pixels,"Missing preview was not rebuilt completely");
    ps::Bitmap differentSize(2,2);ps::updateTransparencyPreview(differentSize,image,mask,{21,20,22,21});
    require(differentSize.pixels==preview.pixels,"Resized preview retained stale image dimensions");
    std::cout<<"PASS: reusable selection sessions, add/subtract counts, bounded preview updates, outline halo, alpha and buffer reuse\n";
}
