#include "selection.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ps {
namespace {
struct Color {float lightness,a,b;};
// Small shared tables avoid pow/cbrt calls and a per-pixel color cache. Selection
// needs one byte per pixel plus pending scanlines, even on large screenshots.
struct ColorTables {
    std::array<float,256> linear{};
    std::array<float,4097> lab{};
    ColorTables() {
        for(size_t i=0;i<linear.size();++i) {
            const float s=static_cast<float>(i)/255;
            linear[i]=s<=0.04045f?s/12.92f:std::pow((s+0.055f)/1.055f,2.4f);
        }
        for(size_t i=0;i<lab.size();++i) {
            const float t=static_cast<float>(i)/4096;
            lab[i]=t>0.008856452f?std::cbrt(t):7.787037f*t+16.0f/116;
        }
    }
    float curve(float value)const {
        const float index=std::clamp(value,0.0f,1.0f)*4096;
        const size_t low=std::min(static_cast<size_t>(index),lab.size()-2);
        return lab[low]+(lab[low+1]-lab[low])*(index-static_cast<float>(low));
    }
    Color color(const uint8_t* pixel)const {
        const float r=linear[pixel[2]],g=linear[pixel[1]],b=linear[pixel[0]];
        const float x=curve((0.4124564f*r+0.3575761f*g+0.1804375f*b)/0.95047f);
        const float y=curve(0.2126729f*r+0.7151522f*g+0.0721750f*b);
        const float z=curve((0.0193339f*r+0.1191920f*g+0.9503041f*b)/1.08883f);
        return {116*y-16,500*(x-y),200*(y-z)};
    }
};
float distanceSquared(Color left,Color right) {
    const float l=(left.lightness-right.lightness)*0.60f;
    const float a=(left.a-right.a)*1.25f,b=(left.b-right.b)*1.25f;
    return l*l+a*a+b*b;
}
float edgeDistanceSquared(Color left,Color right) {
    const float l=left.lightness-right.lightness,a=left.a-right.a,b=left.b-right.b;
    return l*l+a*a+b*b;
}
Color sampleColor(const Bitmap& image,POINT seed,int tolerance,const ColorTables& tables) {
    const Color center=tables.color(image.pixels.data()+(static_cast<size_t>(seed.y)*image.width+seed.x)*4);
    Color sum{center.lightness*4,center.a*4,center.b*4};int weight=4;
    const float radius=std::clamp(tolerance*0.3f,1.0f,5.0f);
    // Smooth compression noise using only neighbors close to the clicked color.
    // A thin line or a click at an edge must not be replaced by the background.
    for(int y=std::max(0,static_cast<int>(seed.y)-1);y<=std::min(image.height-1,static_cast<int>(seed.y)+1);++y)
        for(int x=std::max(0,static_cast<int>(seed.x)-1);x<=std::min(image.width-1,static_cast<int>(seed.x)+1);++x) {
            if(x==seed.x&&y==seed.y)continue;
            const auto* pixel=image.pixels.data()+(static_cast<size_t>(y)*image.width+x)*4;
            if(!pixel[3])continue;
            const Color neighbor=tables.color(pixel);
            if(distanceSquared(center,neighbor)<=radius*radius) {
                sum.lightness+=neighbor.lightness;sum.a+=neighbor.a;sum.b+=neighbor.b;++weight;
            }
        }
    return {sum.lightness/weight,sum.a/weight,sum.b/weight};
}
} // namespace
SelectionMask selectSimilar(const Bitmap& image, POINT seed, int tolerance, bool contiguous) {
    const size_t count=static_cast<size_t>(image.width)*image.height;
    SelectionMask selected(count,0);
    if(seed.x<0||seed.y<0||seed.x>=image.width||seed.y>=image.height)return selected;
    const size_t start=static_cast<size_t>(seed.y)*image.width+seed.x;
    const auto* color=image.pixels.data()+start*4;
    if(!color[3])return selected;
    tolerance=std::clamp(tolerance,0,100);
    static const ColorTables tables;
    const Color reference=sampleColor(image,seed,tolerance,tables);
    const float squaredLimit=static_cast<float>(tolerance*tolerance);
    const auto matches=[&](size_t position,Color sample) {
        const auto* pixel=image.pixels.data()+position*4;
        if(!pixel[3])return false;
        if(tolerance==0)return pixel[0]==color[0]&&pixel[1]==color[1]&&pixel[2]==color[2];
        if(tolerance==100||position==start)return true;
        return distanceSquared(sample,reference)<=squaredLimit;
    };
    if(!contiguous) {
        for(size_t i=0;i<count;++i) {
            const Color sample=tolerance>0&&tolerance<100?tables.color(image.pixels.data()+i*4):Color{};
            if(matches(i,sample))selected[i]=255;
        }
        return selected;
    }
    constexpr uint8_t ELIGIBLE=1,RIGHT_EDGE=2,DOWN_EDGE=4,VISITED=128;
    const bool checkEdges=tolerance>0&&tolerance<100;
    const float edgeLimit=std::clamp(2.0f+tolerance*0.08f,2.5f,4.0f);
    const float squaredEdgeLimit=edgeLimit*edgeLimit;
    const auto supportedEdge=[&](int x1,int y1,int x2,int y2,Color a,Color b) {
        const float strength=edgeDistanceSquared(a,b);
        if(strength<=squaredEdgeLimit)return false;
        const Color direction{a.lightness-b.lightness,a.a-b.a,a.b-b.b};
        // A real outline continues along its tangent. A single compression
        // speckle does not. Include a one-pixel normal offset for rounded or
        // antialiased corners; require a consistent contrast direction.
        const bool horizontal=y1==y2;
        for(int side:{-1,1})for(int offset:{0,-1,1}) {
            const int ax=x1+(horizontal?offset:side),ay=y1+(horizontal?side:offset);
            const int bx=x2+(horizontal?offset:side),by=y2+(horizontal?side:offset);
            if(ax<0||ay<0||bx<0||by<0||ax>=image.width||bx>=image.width||ay>=image.height||by>=image.height)continue;
            const auto* pa=image.pixels.data()+(static_cast<size_t>(ay)*image.width+ax)*4;
            const auto* pb=image.pixels.data()+(static_cast<size_t>(by)*image.width+bx)*4;
            if(!pa[3]||!pb[3])continue;
            const Color ca=tables.color(pa),cb=tables.color(pb);
            const float neighborStrength=edgeDistanceSquared(ca,cb);
            const float dot=direction.lightness*(ca.lightness-cb.lightness)+direction.a*(ca.a-cb.a)+direction.b*(ca.b-cb.b);
            if(neighborStrength>squaredEdgeLimit*0.5f&&dot>0&&dot*dot>=0.5f*strength*neighborStrength)return true;
        }
        return false;
    };
    // Shadow tolerance determines which colors can belong to a region. Local
    // contrast determines where the region ends: a pale one-pixel UI outline
    // must remain a barrier even when its color is close to the seed.
    std::vector<Color> previousRow(checkEdges&&image.height>1?image.width:0);
    for(int y=0;y<image.height;++y) {
        Color left{};
        for(int x=0;x<image.width;++x) {
            const size_t i=static_cast<size_t>(y)*image.width+x;
            const Color sample=checkEdges?tables.color(image.pixels.data()+i*4):Color{};
            if(matches(i,sample))selected[i]=ELIGIBLE;
            if(checkEdges) {
                if(x>0&&(selected[i]&ELIGIBLE)&&(selected[i-1]&ELIGIBLE)&&
                   supportedEdge(x-1,y,x,y,left,sample))selected[i-1]|=RIGHT_EDGE;
                if(!previousRow.empty()) {
                    if(y>0&&(selected[i]&ELIGIBLE)&&(selected[i-image.width]&ELIGIBLE)&&
                       supportedEdge(x,y-1,x,y,previousRow[x],sample))selected[i-image.width]|=DOWN_EDGE;
                    previousRow[x]=sample;
                }
                left=sample;
            }
        }
    }
    // Four-connected, edge-bounded scanlines cannot escape through diagonal
    // corner contacts. Edge bits survive marking a span as visited.
    struct Span {int left,right,y;};std::vector<Span> pending;
    const auto unvisited=[](uint8_t pixel){return (pixel&(ELIGIBLE|VISITED))==ELIGIBLE;};
    const auto addSpan=[&](int x,int y) {
        const size_t row=static_cast<size_t>(y)*image.width;
        int left=x,right=x;
        while(left>0&&unvisited(selected[row+left-1])&&!(selected[row+left-1]&RIGHT_EDGE))--left;
        while(right+1<image.width&&unvisited(selected[row+right+1])&&!(selected[row+right]&RIGHT_EDGE))++right;
        for(int i=left;i<=right;++i)selected[row+i]|=VISITED;
        pending.push_back({left,right,y});return right;
    };
    addSpan(seed.x,seed.y);
    while(!pending.empty()) {
        const Span span=pending.back();pending.pop_back();
        for(int y:{span.y-1,span.y+1}) {
            if(y<0||y>=image.height)continue;
            const size_t row=static_cast<size_t>(y)*image.width;
            const size_t edgeRow=static_cast<size_t>(std::min(y,span.y))*image.width;
            for(int x=span.left;x<=span.right;++x)
                if(unvisited(selected[row+x])&&!(selected[edgeRow+x]&DOWN_EDGE))x=addSpan(x,y);
        }
    }
    for(auto& value:selected)value=(value&VISITED)?255:0;
    return selected;
}
bool brushSimilar(const Bitmap& image,SelectionMask& selection,POINT from,POINT to,
                  int radius,int tolerance,bool contiguous,bool subtract) {
    if(image.empty())return false;
    const size_t count=static_cast<size_t>(image.width)*image.height;
    if(selection.size()!=count)selection.assign(count,0);
    radius=std::clamp(radius,1,64);
    const double dx=static_cast<double>(to.x)-from.x,dy=static_cast<double>(to.y)-from.y;
    double first=0,last=1;
    const auto clip=[&](double p,double q) {
        if(p==0)return q>=0;
        const double t=q/p;
        if(p<0){if(t>last)return false;first=std::max(first,t);}
        else {if(t<first)return false;last=std::min(last,t);}
        return true;
    };
    // Bound off-image drags before interpolation, including the brush radius.
    if(!clip(-dx,static_cast<double>(from.x)+radius)||!clip(dx,image.width-1.0+radius-from.x)||
       !clip(-dy,static_cast<double>(from.y)+radius)||!clip(dy,image.height-1.0+radius-from.y))return false;
    const double ax=from.x+dx*first,ay=from.y+dy*first;
    const double bx=from.x+dx*last,by=from.y+dy*last;
    const int steps=static_cast<int>(std::ceil(std::hypot(bx-ax,by-ay)/std::max(1,radius/2)));
    bool changed=false;
    for(int step=0;step<=steps;++step) {
        const double t=steps?static_cast<double>(step)/steps:0;
        const int cx=static_cast<int>(std::lround(ax+(bx-ax)*t)),cy=static_cast<int>(std::lround(ay+(by-ay)*t));
        for(int y=std::max(0,cy-radius);y<=std::min(image.height-1,cy+radius);++y) {
            const int half=static_cast<int>(std::sqrt(radius*radius-(y-cy)*(y-cy)));
            for(int x=std::max(0,cx-half);x<=std::min(image.width-1,cx+half);++x) {
                const size_t i=static_cast<size_t>(y)*image.width+x;
                const bool alreadyDone=subtract?selection[i]==0:selection[i]!=0;
                if(!image.pixels[i*4+3]||alreadyDone)continue;
                const auto region=selectSimilar(image,{x,y},tolerance,contiguous);
                combineSelection(selection,region,subtract);changed=true;
            }
        }
    }
    return changed;
}
void combineSelection(SelectionMask& destination,const SelectionMask& source,bool subtract) {
    if(destination.size()!=source.size())destination.assign(source.size(),0);
    for(size_t i=0;i<source.size();++i)destination[i]=subtract?static_cast<uint8_t>(destination[i]*(255-source[i])/255):std::max(destination[i],source[i]);
}
void invertSelection(SelectionMask& selection,const Bitmap& image) {
    const size_t count=static_cast<size_t>(image.width)*image.height;
    if(selection.size()!=count)selection.assign(count,0);
    for(size_t i=0;i<count;++i)selection[i]=image.pixels[i*4+3]?static_cast<uint8_t>(255-selection[i]):0;
}
SelectionMask reframeMask(const SelectionMask& mask,RECT from,RECT to,uint8_t fill) {
    const int w=to.right-to.left,h=to.bottom-to.top,oldWidth=from.right-from.left,oldHeight=from.bottom-from.top;
    if(w<=0||h<=0||oldWidth<=0||oldHeight<=0||mask.size()!=static_cast<size_t>(oldWidth)*oldHeight)
        throw std::runtime_error("Invalid mask crop dimensions");
    SelectionMask result(static_cast<size_t>(w)*h,fill);RECT overlap{};
    if(IntersectRect(&overlap,&from,&to))for(int y=overlap.top;y<overlap.bottom;++y)
        std::copy_n(mask.begin()+static_cast<size_t>(y-from.top)*oldWidth+overlap.left-from.left,overlap.right-overlap.left,
            result.begin()+static_cast<size_t>(y-to.top)*w+overlap.left-to.left);
    return result;
}
RECT contentBounds(const Bitmap& image,const SelectionMask& selection) {
    if(image.empty())return {};
    if(!selection.empty()&&selection.size()!=static_cast<size_t>(image.width)*image.height)
        throw std::runtime_error("Content mask dimensions do not match image");
    RECT bounds{image.width,image.height,0,0};
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        const size_t i=static_cast<size_t>(y)*image.width+x;
        if(image.pixels[i*4+3]&&(selection.empty()||selection[i])) {
            bounds.left=std::min<LONG>(bounds.left,x);bounds.top=std::min<LONG>(bounds.top,y);
            bounds.right=std::max<LONG>(bounds.right,x+1);bounds.bottom=std::max<LONG>(bounds.bottom,y+1);
        }
    }
    return bounds.left<bounds.right&&bounds.top<bounds.bottom?bounds:RECT{};
}
SelectionMask cutoutOpacity(const SelectionMask& selection,int width,int height,bool keep,bool soften) {
    if(selection.size()!=static_cast<size_t>(width)*height)throw std::runtime_error("Selection dimensions do not match image");
    SelectionMask opacity(selection.size());
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const size_t i=static_cast<size_t>(y)*width+x;int coverage=selection[i];
        if(soften) {
            int sum=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                int xx=std::clamp(x+dx,0,width-1),yy=std::clamp(y+dy,0,height-1);
                int weight=(dx==0?2:1)*(dy==0?2:1);sum+=selection[static_cast<size_t>(yy)*width+xx]*weight;
            }
            const int blurred=(sum+8)/16;
            // Feather inside the retained region. Blurring both ways would
            // restore removed background pixels and create a pale fringe.
            coverage=keep?std::min(coverage,blurred):std::max(coverage,blurred);
        }
        opacity[i]=static_cast<uint8_t>(keep?coverage:255-coverage);
    }
    return opacity;
}
Bitmap transparencyPreview(const Bitmap& image,const SelectionMask& selection) {
    Bitmap result(image.width,image.height);
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
        const size_t index=static_cast<size_t>(y)*image.width+x,i=index*4;
        const unsigned alpha=image.pixels[i+3];const unsigned background=((x/8+y/8)%2)?204:238;
        for(size_t channel=0;channel<3;++channel)
            result.pixels[i+channel]=static_cast<uint8_t>((image.pixels[i+channel]*alpha+background*(255-alpha)+127)/255);
        if(selection.size()==static_cast<size_t>(image.width)*image.height&&selection[index]) {
            constexpr unsigned blue[3]={255,159,66};
            for(size_t channel=0;channel<3;++channel)result.pixels[i+channel]=static_cast<uint8_t>((result.pixels[i+channel]*2+blue[channel])/3);
            const bool edge=x==0||y==0||x+1==image.width||y+1==image.height||
                !selection[index-1]||!selection[index+1]||!selection[index-image.width]||!selection[index+image.width];
            if(edge)for(size_t channel=0;channel<3;++channel)result.pixels[i+channel]=((x+y)%8<4)?255:32;
        }
        result.pixels[i+3]=255;
    }
    return result;
}
} // namespace ps
