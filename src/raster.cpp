#include "image.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ps {
void pixelatePremultiplied(uint8_t* pixels,size_t stride,RECT region,int blockSize) {
    const int block=std::clamp(blockSize,MinMosaicBlock,MaxMosaicBlock);
    for(int y=region.top;y<region.bottom;y+=block)for(int x=region.left;x<region.right;x+=block) {
        const int bottom=std::min<LONG>(y+block,region.bottom),right=std::min<LONG>(x+block,region.right);
        std::array<unsigned,4> sums{};
        for(int yy=y;yy<bottom;++yy)for(int xx=x;xx<right;++xx) {
            const auto* pixel=pixels+static_cast<size_t>(yy)*stride+xx*4;
            for(size_t channel=0;channel<sums.size();++channel)sums[channel]+=pixel[channel];
        }
        for(int yy=y;yy<bottom;++yy)for(int xx=x;xx<right;++xx) {
            auto* pixel=pixels+static_cast<size_t>(yy)*stride+xx*4;
            for(size_t channel=0;channel<3;++channel)
                pixel[channel]=sums[3]?static_cast<uint8_t>((sums[channel]*pixel[3]+sums[3]/2)/sums[3]):0;
        }
    }
}

PenCoverage::PenCoverage(int width,int height,float thickness)
    :width_(width),height_(height),radius_(thickness/2),
     coverage_(static_cast<size_t>(width)*height,0) {}

RECT PenCoverage::extend(POINT from,POINT to) {
    const float outer=radius_+0.5f;
    const int padding=static_cast<int>(std::ceil(outer));
    const int top=std::max<LONG>(0,std::min(from.y,to.y)-padding);
    const int bottom=std::min<LONG>(height_,std::max(from.y,to.y)+padding+1);
    const float dx=static_cast<float>(to.x-from.x),dy=static_cast<float>(to.y-from.y);
    const float lengthSquared=dx*dx+dy*dy;
    RECT dirty{width_,height_,0,0};
    for(int y=top;y<bottom;++y) {
        const float sampleY=y+0.5f;
        // Only walk the part of the segment near this row, even on a long diagonal.
        float first=0,last=1;
        if(dy!=0) {
            const float a=(sampleY-from.y-outer)/dy,b=(sampleY-from.y+outer)/dy;
            first=std::clamp(std::min(a,b),0.0f,1.0f);
            last=std::clamp(std::max(a,b),0.0f,1.0f);
        }
        const float a=from.x+dx*first,b=from.x+dx*last;
        const int left=std::max(0,static_cast<int>(std::floor(std::min(a,b)-outer)));
        const int right=std::min(width_,static_cast<int>(std::ceil(std::max(a,b)+outer)));
        for(int x=left;x<right;++x) {
            const float ox=x+0.5f-from.x,oy=sampleY-from.y;
            const float t=lengthSquared?std::clamp((ox*dx+oy*dy)/lengthSquared,0.0f,1.0f):0.0f;
            const float distance=std::hypot(ox-t*dx,oy-t*dy);
            const auto value=static_cast<uint8_t>(std::lround(255*std::clamp(outer-distance,0.0f,1.0f)));
            auto& pixel=coverage_[static_cast<size_t>(y)*width_+x];
            if(value<=pixel)continue;
            pixel=value;
            dirty.left=std::min<LONG>(dirty.left,x);
            dirty.right=std::max<LONG>(dirty.right,x+1);
            dirty.top=std::min<LONG>(dirty.top,y);
            dirty.bottom=std::max<LONG>(dirty.bottom,y+1);
        }
    }
    if(IsRectEmpty(&dirty))return {};
    RECT combined{};
    UnionRect(&combined,&bounds_,&dirty);
    bounds_=combined;
    return dirty;
}

void PenCoverage::blend(uint8_t* destination,size_t stride,const uint8_t* source,RECT region,uint32_t color) const {
    const std::array<unsigned,4> ink{color&255,(color>>8)&255,(color>>16)&255,255};
    for(int y=region.top;y<region.bottom;++y)for(int x=region.left;x<region.right;++x) {
        const size_t index=static_cast<size_t>(y)*width_+x;
        const unsigned alpha=coverage_[index];
        if(!alpha)continue;
        auto* pixel=destination+static_cast<size_t>(y)*stride+x*4;
        const auto* background=source?source+index*4:pixel;
        for(size_t channel=0;channel<ink.size();++channel)
            pixel[channel]=static_cast<uint8_t>((ink[channel]*alpha+background[channel]*(255-alpha)+127)/255);
    }
}
} // namespace ps
