#include "smart_cutout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace ps {
namespace {
using Color = std::array<float,3>;
enum Mark : uint8_t { Unknown, Keep, Remove };
constexpr int MaxGraphSide=448;
constexpr float Unlimited=std::numeric_limits<float>::max();

int width(RECT rect) {return rect.right-rect.left;}
int height(RECT rect) {return rect.bottom-rect.top;}
Color colorAt(const Bitmap& image,int x,int y) {
    const auto* pixel=image.pixels.data()+(static_cast<size_t>(y)*image.width+x)*4;
    return {static_cast<float>(pixel[0]),static_cast<float>(pixel[1]),static_cast<float>(pixel[2])};
}
bool opaqueAt(const Bitmap& image,int x,int y) {
    return image.pixels[(static_cast<size_t>(y)*image.width+x)*4+3]!=0;
}
float squaredDistance(const Color& a,const Color& b) {
    float total=0;
    for(size_t c=0;c<a.size();++c)total+=(a[c]-b[c])*(a[c]-b[c]);
    return total/3;
}

struct Sample {
    Color color;
    float x,y;
    float weight=1;
};
Sample sampleAt(const Bitmap& image,int x,int y,float weight=1) {
    return {colorAt(image,x,y),2.0f*x/std::max(1,image.width-1)-1,
        2.0f*y/std::max(1,image.height-1)-1,weight};
}

class Palette {
    std::vector<Color> colors_;
public:
    explicit Palette(const std::vector<Sample>& samples) {
        if(samples.empty())return;
        colors_.push_back(samples.front().color);
        std::vector<float> nearest(samples.size(),Unlimited);
        while(colors_.size()<8) {
            for(size_t i=0;i<samples.size();++i)
                nearest[i]=std::min(nearest[i],squaredDistance(colors_.back(),samples[i].color));
            const auto farthest=std::max_element(nearest.begin(),nearest.end());
            if(*farthest<9)break;
            colors_.push_back(samples[farthest-nearest.begin()].color);
        }
        for(int pass=0;pass<5;++pass) {
            std::array<Color,8> sum{};
            std::array<float,8> weight{};
            for(const auto& point:samples) {
                const size_t slot=closest(point.color);
                for(size_t c=0;c<3;++c)sum[slot][c]+=point.color[c]*point.weight;
                weight[slot]+=point.weight;
            }
            for(size_t i=0;i<colors_.size();++i)if(weight[i]>0)
                for(size_t c=0;c<3;++c)colors_[i][c]=sum[i][c]/weight[i];
        }
    }
    size_t closest(const Color& color) const {
        size_t result=0;
        float distance=Unlimited;
        for(size_t i=0;i<colors_.size();++i) {
            const float candidate=squaredDistance(color,colors_[i]);
            if(candidate<distance){result=i;distance=candidate;}
        }
        return result;
    }
    float distance(const Color& color) const {
        return std::sqrt(squaredDistance(color,colors_[closest(color)]));
    }
};

struct BackgroundPlane {
    std::array<Color,3> coefficient{};
    float noise=0;
    bool valid=false;

    Color predict(float x,float y) const {
        Color result{};
        for(size_t c=0;c<3;++c)
            result[c]=std::clamp(coefficient[0][c]+coefficient[1][c]*x+coefficient[2][c]*y,0.0f,255.0f);
        return result;
    }
    explicit BackgroundPlane(const std::vector<Sample>& samples) {
        if(samples.size()<12)return;
        for(int pass=0;pass<4;++pass) {
            double matrix[3][6]{};
            for(const auto& point:samples) {
                const double basis[3]{1,point.x,point.y};
                const double error=pass?std::sqrt(squaredDistance(point.color,predict(point.x,point.y))):0;
                const double weight=point.weight*(error<=5?1.0:5.0/error);
                for(int y=0;y<3;++y) {
                    for(int x=0;x<3;++x)matrix[y][x]+=weight*basis[y]*basis[x];
                    for(int c=0;c<3;++c)matrix[y][3+c]+=weight*basis[y]*point.color[c];
                }
            }
            for(int i=0;i<3;++i) {
                int pivot=i;
                for(int j=i+1;j<3;++j)if(std::abs(matrix[j][i])>std::abs(matrix[pivot][i]))pivot=j;
                if(std::abs(matrix[pivot][i])<1e-5)return;
                for(int c=i;c<6;++c)std::swap(matrix[pivot][c],matrix[i][c]);
                const double scale=matrix[i][i];
                for(int c=i;c<6;++c)matrix[i][c]/=scale;
                for(int j=0;j<3;++j)if(j!=i) {
                    const double multiplier=matrix[j][i];
                    for(int c=i;c<6;++c)matrix[j][c]-=multiplier*matrix[i][c];
                }
            }
            for(int i=0;i<3;++i)for(int c=0;c<3;++c)coefficient[i][c]=static_cast<float>(matrix[i][3+c]);
        }
        std::vector<float> residuals;
        residuals.reserve(samples.size());
        for(const auto& point:samples)
            residuals.push_back(std::sqrt(squaredDistance(point.color,predict(point.x,point.y))));
        const auto middle=residuals.begin()+residuals.size()/2;
        std::nth_element(residuals.begin(),middle,residuals.end());
        noise=*middle;
        const auto inliers=std::count_if(residuals.begin(),residuals.end(),[](float error){return error<=9;});
        valid=noise<=5&&inliers*4>=static_cast<ptrdiff_t>(samples.size())*3;
    }
    float distance(const Sample& point,const Palette& palette) const {
        const float paletteError=palette.distance(point.color);
        if(!valid)return paletteError;
        const float spatial=std::sqrt(squaredDistance(point.color,predict(point.x,point.y)));
        return std::min(spatial,13.0f+0.55f*spatial+0.45f*paletteError);
    }
};

struct Grid {
    RECT area;
    int step,w,h;
    explicit Grid(RECT region):area(region),
        step(std::max(1,(std::max(width(area),height(area))+MaxGraphSide-1)/MaxGraphSide)),
        w((width(area)+step-1)/step),h((height(area)+step-1)/step) {}
    int index(int x,int y) const {return y*w+x;}
    POINT screen(int x,int y) const {
        return {std::min<LONG>(area.right-1,area.left+x*step+step/2),
                std::min<LONG>(area.bottom-1,area.top+y*step+step/2)};
    }
    int screenIndex(int x,int y) const {return index((x-area.left)/step,(y-area.top)/step);}
    POINT cell(POINT p) const {
        return {static_cast<LONG>(std::floor(static_cast<double>(p.x-area.left)/step)),
                static_cast<LONG>(std::floor(static_cast<double>(p.y-area.top)/step))};
    }
};

std::vector<Mark> rasterMarks(const Grid& grid,const std::vector<CutoutStroke>& strokes) {
    std::vector<Mark> labels(static_cast<size_t>(grid.w)*grid.h,Unknown);
    for(const auto& stroke:strokes) {
        if(stroke.points.empty())continue;
        const int radius=std::max(0,(std::clamp(stroke.radius,1,64)-1)/grid.step);
        const Mark mark=stroke.keep?Keep:Remove;
        POINT from=grid.cell(stroke.points.front());
        for(auto point:stroke.points) {
            const POINT to=grid.cell(point);
            const int dx=to.x-from.x,dy=to.y-from.y;
            const int steps=std::max({1,std::abs(dx),std::abs(dy)});
            for(int i=0;i<=steps;++i) {
                const int cx=from.x+static_cast<int>(std::lround(static_cast<double>(dx)*i/steps));
                const int cy=from.y+static_cast<int>(std::lround(static_cast<double>(dy)*i/steps));
                for(int y=std::max(0,cy-radius);y<=std::min(grid.h-1,cy+radius);++y) {
                    const int half=static_cast<int>(std::sqrt(radius*radius-(y-cy)*(y-cy)));
                    for(int x=std::max(0,cx-half);x<=std::min(grid.w-1,cx+half);++x)
                        labels[grid.index(x,y)]=mark;
                }
            }
            from=to;
        }
    }
    return labels;
}

class FlowGraph {
    struct Edge {int to,next,capacity;};
    std::vector<Edge> edges_;
    std::vector<int> first_,distance_,current_,queue_,path_,limit_;
    int source_,sink_;
public:
    explicit FlowGraph(int nodes):first_(nodes+2,-1),distance_(nodes+2),current_(nodes+2),
        source_(nodes),sink_(nodes+1) {
        edges_.reserve(static_cast<size_t>(nodes)*8);
        queue_.reserve(nodes+2);
    }
    void edge(int from,int to,int capacity,int reverse=0) {
        edges_.push_back({to,first_[from],capacity});first_[from]=static_cast<int>(edges_.size()-1);
        edges_.push_back({from,first_[to],reverse});first_[to]=static_cast<int>(edges_.size()-1);
    }
    void terminal(int node,int keep,int remove) {
        if(keep)edge(source_,node,keep);
        if(remove)edge(node,sink_,remove);
    }
    SelectionMask solve() {
        while(levels()) {
            current_=first_;
            while(augment()) {}
        }
        SelectionMask foreground(source_);
        for(int i=0;i<source_;++i)foreground[i]=distance_[i]>=0?255:0;
        return foreground;
    }
private:
    bool levels() {
        std::fill(distance_.begin(),distance_.end(),-1);
        queue_.clear();queue_.push_back(source_);distance_[source_]=0;
        for(size_t i=0;i<queue_.size();++i) {
            const int node=queue_[i];
            if(distance_[sink_]>=0&&distance_[node]>=distance_[sink_])break;
            for(int link=first_[node];link>=0;link=edges_[link].next) {
                const auto& edge=edges_[link];
                if(edge.capacity&&distance_[edge.to]<0) {
                    distance_[edge.to]=distance_[node]+1;queue_.push_back(edge.to);
                }
            }
        }
        return distance_[sink_]>=0;
    }
    bool augment() {
        int node=source_;
        path_.clear();limit_.clear();limit_.push_back(std::numeric_limits<int>::max());
        while(node!=sink_) {
            int& link=current_[node];
            while(link>=0&&(!edges_[link].capacity||distance_[edges_[link].to]!=distance_[node]+1))
                link=edges_[link].next;
            if(link>=0) {
                const auto& edge=edges_[link];
                path_.push_back(link);limit_.push_back(std::min(limit_.back(),edge.capacity));
                node=edge.to;
                continue;
            }
            if(path_.empty())return false;
            distance_[node]=-1;
            const int linkToNode=path_.back();path_.pop_back();limit_.pop_back();
            node=edges_[linkToNode^1].to;
            current_[node]=edges_[linkToNode].next;
        }
        for(int link:path_) {
            edges_[link].capacity-=limit_.back();
            edges_[link^1].capacity+=limit_.back();
        }
        return true;
    }
};

void collectBackground(const Bitmap& image,RECT subject,std::vector<Sample>& background) {
    const int sx=std::max(1,image.width/60),sy=std::max(1,image.height/60);
    const auto collect=[&](int x,int y) {
        if(x>=0&&y>=0&&x<image.width&&y<image.height&&opaqueAt(image,x,y)&&!PtInRect(&subject,{x,y}))
            background.push_back(sampleAt(image,x,y));
    };
    for(int y=0;y<image.height;y+=sy)for(int x=0;x<image.width;x+=sx)collect(x,y);
    for(int x=0;x<image.width;x+=sx){collect(x,subject.top-1);collect(x,subject.bottom);collect(x,image.height-1);}
    for(int y=0;y<image.height;y+=sy){collect(subject.left-1,y);collect(subject.right,y);collect(image.width-1,y);}
}

SelectionMask edgeCells(const Grid& grid,const SelectionMask& coarse) {
    SelectionMask result(coarse.size());
    const int radius=(std::clamp(2*grid.step,2,6)+grid.step-1)/grid.step;
    for(int y=0;y<grid.h;++y)for(int x=0;x<grid.w;++x) {
        const auto value=coarse[grid.index(x,y)];
        const bool edge=(x?coarse[grid.index(x-1,y)]:0)!=value||
            (y?coarse[grid.index(x,y-1)]:0)!=value||
            (x+1<grid.w?coarse[grid.index(x+1,y)]:0)!=value||
            (y+1<grid.h?coarse[grid.index(x,y+1)]:0)!=value;
        if(!edge)continue;
        for(int yy=std::max(0,y-radius);yy<=std::min(grid.h-1,y+radius);++yy)
            for(int xx=std::max(0,x-radius);xx<=std::min(grid.w-1,x+radius);++xx)
                result[grid.index(xx,yy)]=255;
    }
    return result;
}

uint8_t refineAlpha(const Bitmap& image,int x,int y,const Grid& grid,const SelectionMask& coarse,
                    const BackgroundPlane& plane) {
    const auto label=[&](int px,int py) -> uint8_t {
        return px>=0&&py>=0&&px<image.width&&py<image.height&&PtInRect(&grid.area,{px,py})?
            coarse[grid.screenIndex(px,py)]:0;
    };
    const uint8_t center=label(x,y);
    const int radius=std::clamp(2*grid.step,2,6);
    constexpr std::array<POINT,8> directions{{{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}}};
    int fgCount=0,bgCount=0;
    Color fg{},bg{};
    for(auto d:directions) {
        const int sx=x+d.x*radius,sy=y+d.y*radius;
        if(sx<0||sy<0||sx>=image.width||sy>=image.height||!opaqueAt(image,sx,sy))continue;
        const auto sample=colorAt(image,sx,sy);
        if(label(sx,sy)) {
            for(size_t c=0;c<3;++c)fg[c]+=sample[c];
            ++fgCount;
        } else {
            for(size_t c=0;c<3;++c)bg[c]+=sample[c];
            ++bgCount;
        }
    }
    if(!fgCount||!bgCount)return center;
    const auto sample=sampleAt(image,x,y);
    if(plane.valid)bg=plane.predict(sample.x,sample.y);
    else for(float& c:bg)c/=bgCount;
    for(float& c:fg)c/=fgCount;
    float dot=0,span=0;
    for(size_t c=0;c<3;++c) {
        const float delta=fg[c]-bg[c];
        dot+=(sample.color[c]-bg[c])*delta;span+=delta*delta;
    }
    if(span<3*8*8)return center;
    const float alpha=std::clamp(dot/span,0.0f,1.0f);
    const float normalized=std::clamp((alpha-0.025f)/0.95f,0.0f,1.0f);
    return static_cast<uint8_t>(std::lround(255*normalized));
}
}

SelectionMask smartCutout(const Bitmap& image,RECT subject,const std::vector<CutoutStroke>& strokes,bool refine) {
    const RECT frame{0,0,image.width,image.height};
    if(image.empty()||!IntersectRect(&subject,&subject,&frame)||width(subject)<3||height(subject)<3)return {};

    RECT area=subject;
    // Keep strokes may recover the immediately adjacent pixels near a tight box.
    for(const auto& stroke:strokes)if(stroke.keep)for(auto point:stroke.points) {
        const LONG margin=std::clamp(stroke.radius,1,64)*2;
        RECT patch{point.x-margin,point.y-margin,point.x+margin+1,point.y+margin+1};
        UnionRect(&area,&area,&patch);
    }
    IntersectRect(&area,&area,&frame);
    const Grid grid(area);
    const int nodes=grid.w*grid.h;
    const auto marks=rasterMarks(grid,strokes);
    std::vector<Sample> samples; samples.reserve(nodes);
    std::vector<Sample> background,foreground;
    collectBackground(image,subject,background);
    const BackgroundPlane plane(background);
    for(int y=0;y<grid.h;++y)for(int x=0;x<grid.w;++x) {
        const auto center=grid.screen(x,y);
        samples.push_back(sampleAt(image,center.x,center.y));
        if(!opaqueAt(image,center.x,center.y))continue;
        const auto mark=marks[grid.index(x,y)];
        if(mark==Remove&&background.size()<10000)background.push_back(sampleAt(image,center.x,center.y,3));
        if(mark==Keep&&foreground.size()<5000)foreground.push_back(sampleAt(image,center.x,center.y,4));
    }
    // A cell-center sample can entirely miss a one-pixel logo stroke. With a
    // reliable gradient model, retain the strongest foreground evidence in
    // each coarse cell instead. Storage and graph size stay bounded by Grid.
    if(grid.step>1&&plane.valid) {
        const float evidence=std::max(12.0f,plane.noise*4);
        std::vector<float> strongest(nodes,evidence*evidence);
        for(int y=area.top;y<area.bottom;++y)for(int x=area.left;x<area.right;++x) {
            const int i=grid.screenIndex(x,y);
            if(marks[i]!=Unknown||!opaqueAt(image,x,y))continue;
            const auto sample=sampleAt(image,x,y);
            const float contrast=squaredDistance(sample.color,plane.predict(sample.x,sample.y));
            if(contrast>strongest[i]){strongest[i]=contrast;samples[i]=sample;}
        }
    }
    if(background.empty())return {};
    const Palette bgPalette(background);
    const float threshold=plane.valid?std::max(6.0f,plane.noise*2.8f):13.0f;
    const int stride=std::max(1,nodes/6500);
    for(int i=0;i<nodes;i+=stride) {
        const auto p=grid.screen(i%grid.w,i/grid.w);
        if(marks[i]!=Unknown||!PtInRect(&subject,p)||!opaqueAt(image,p.x,p.y))continue;
        if(plane.distance(samples[i],bgPalette)>threshold)foreground.push_back(samples[i]);
    }
    // Sparse details can also fall between the foreground-training samples.
    // Recover their evidence without increasing the graph to full resolution.
    if(foreground.empty()&&grid.step>1&&plane.valid) {
        for(int i=0;i<nodes&&foreground.size()<6500;++i) {
            const auto p=grid.screen(i%grid.w,i/grid.w);
            if(marks[i]==Unknown&&PtInRect(&subject,p)&&opaqueAt(image,p.x,p.y)&&
               plane.distance(samples[i],bgPalette)>threshold)foreground.push_back(samples[i]);
        }
    }
    if(foreground.empty())return {};
    const Palette fgPalette(foreground);
    FlowGraph graph(nodes);
    const auto join=[&](int a,int b) {
        // With a reliable gradient model, a narrow contrasting feature has
        // stronger evidence than its small area would otherwise contribute.
        const float smoothness=plane.valid?26.0f:37.0f;
        const float contrast=plane.valid?200.0f:450.0f;
        const int strength=1+static_cast<int>(smoothness*std::exp(-squaredDistance(samples[a].color,samples[b].color)/contrast));
        graph.edge(a,b,strength,strength);
    };
    for(int y=0;y<grid.h;++y)for(int x=0;x<grid.w;++x) {
        const int i=grid.index(x,y);
        const auto p=grid.screen(x,y);
        if(!opaqueAt(image,p.x,p.y)||marks[i]==Remove)graph.terminal(i,0,100000);
        else if(marks[i]==Keep)graph.terminal(i,100000,0);
        else {
            const auto& sample=samples[i];
            const float nearBackground=plane.distance(sample,bgPalette);
            const float preference=nearBackground-fgPalette.distance(sample.color)-3.5f;
            const int score=static_cast<int>(std::lround(4*std::clamp(preference,-80.0f,80.0f)));
            const int outside=PtInRect(&subject,p)?0:90;
            graph.terminal(i,std::max(0,score),std::max(0,-score)+outside);
        }
        if(x)join(i,i-1);
        if(y)join(i,i-grid.w);
    }
    const auto coarse=graph.solve();
    if(std::none_of(coarse.begin(),coarse.end(),[](uint8_t alpha){return alpha!=0;}))return {};
    SelectionMask result(static_cast<size_t>(image.width)*image.height);
    const auto nearBoundary=refine?edgeCells(grid,coarse):SelectionMask{};
    // Evaluate edges in original screen pixels, including a narrow band
    // outside the rectangle where mixed pixels may belong to a soft edge.
    RECT bounds=area;
    if(refine)InflateRect(&bounds,2,2);
    IntersectRect(&bounds,&bounds,&frame);
    for(int y=bounds.top;y<bounds.bottom;++y)for(int x=bounds.left;x<bounds.right;++x) {
        if(!opaqueAt(image,x,y))continue;
        const int cell=PtInRect(&area,{x,y})?grid.screenIndex(x,y):-1;
        const Mark mark=cell>=0?marks[cell]:Unknown;
        uint8_t alpha=cell>=0?coarse[cell]:0;
        if(mark!=Unknown)alpha=mark==Keep?255:0;
        else if(refine&&(cell<0||nearBoundary[cell]))alpha=refineAlpha(image,x,y,grid,coarse,plane);
        // Do not turn a whole coarse foreground cell into a thick stroke.
        // Restore background pixels using the original-resolution color;
        // explicit keep/remove marks retain their existing priority.
        if(grid.step>1&&plane.valid&&mark==Unknown&&alpha) {
            const auto sample=sampleAt(image,x,y);
            const float noise=std::max(3.0f,plane.noise*2);
            if(squaredDistance(sample.color,plane.predict(sample.x,sample.y))<=noise*noise)alpha=0;
        }
        result[static_cast<size_t>(y)*image.width+x]=alpha;
    }
    return result;
}
}
