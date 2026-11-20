#include "smart_cutout.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void requireSmart(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
}

void testSmartCutout() {
    constexpr int w=224,h=144;
    ps::Bitmap image(w,h);
    std::vector<uint8_t> expected(w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        int gray=70+(x*144)/w+(y*18)/h;
        const float dx=(x-138)/38.0f,dy=(y-73)/34.0f;
        const float coverage=std::clamp((1-std::sqrt(dx*dx+dy*dy))*26+0.5f,0.0f,1.0f);
        const size_t i=static_cast<size_t>(y)*w+x;
        expected[i]=static_cast<uint8_t>(std::lround(coverage*255));
        for(int c=0;c<3;++c)image.pixels[i*4+c]=static_cast<uint8_t>(std::lround(gray+(94-gray)*coverage));
        image.pixels[i*4+3]=255;
    }
    const RECT subject{87,29,187,116};
    const auto alpha=ps::smartCutout(image,subject,{},true);
    const auto hard=ps::smartCutout(image,subject,{},false);
    requireSmart(alpha.size()==expected.size()&&hard.size()==expected.size(),"Smart cutout did not produce a full-size mask");
    int solid=0,kept=0,empty=0,cleared=0,edges=0,soft=0,softError=0,hardError=0;
    for(size_t i=0;i<expected.size();++i) {
        if(expected[i]==255){++solid;kept+=alpha[i]>=245;}
        else if(!expected[i]){++empty;cleared+=alpha[i]<=8;}
        else {
            ++edges;soft+=alpha[i]>0&&alpha[i]<255;
            softError+=std::abs(int(alpha[i])-expected[i]);hardError+=std::abs(int(hard[i])-expected[i]);
        }
    }
    std::cout<<"Gradient foreground retained "<<kept<<"/"<<solid<<", background cleared "<<cleared<<"/"<<empty
        <<", soft edge samples "<<soft<<"/"<<edges<<" and alpha error "<<softError<<" vs "<<hardError<<" without refine\n";
    requireSmart(kept>solid*0.97&&cleared>empty*0.995,"Gradient-aware cutout lost solid pixels or included distant background");
    requireSmart(soft>edges*0.50&&softError<hardError,"Edge refinement did not improve graded transparency");

    const auto fixed=ps::selectSimilar(image,{2,2},18,true,true);
    int legacyBackground=0,legacySpill=0;
    for(size_t i=0;i<expected.size();++i) {
        if(!expected[i])legacyBackground+=fixed[i]!=0;
        if(expected[i]==255)legacySpill+=fixed[i]!=0;
    }
    std::cout<<"Wand background removed "<<legacyBackground<<"/"<<empty<<" and erased "<<legacySpill<<"/"<<solid<<" solid pixels\n";

    const std::vector<ps::CutoutStroke> corrections{{false,3,{{132,69},{143,69}}},{true,2,{{137,69}}}};
    const auto corrected=ps::smartCutout(image,subject,corrections,true);
    requireSmart(corrected[69*w+132]==0&&corrected[69*w+137]==255&&corrected[69*w+143]==0,
        "Keep/remove marks did not override the result in stroke order");
    const auto fullImage=ps::smartCutout(image,{0,0,w,h},{{true,3,{{138,73}}},{false,3,{{5,5},{5,40}}}},false);
    requireSmart(!fullImage.empty()&&fullImage[73*w+138]==255&&fullImage[5*w+5]==0,
        "Full-frame cutout could not learn from explicitly marked background");
    requireSmart(ps::smartCutout(image,{0,0,w,h},{},true).empty(),
        "Missing background training samples produced a destructive result");
    image.pixels[(73*w+138)*4+3]=0;
    const auto transparent=ps::smartCutout(image,subject,{{true,3,{{138,73}}}},true);
    requireSmart(!transparent.empty()&&transparent[73*w+138]==0,"Cutout restored a previously transparent source pixel");
    ps::Bitmap colored(320,160);
    int wanted=0;
    for(int y=0;y<160;++y)for(int x=0;x<320;++x) {
        const bool logo=((x==126||x==164)&&(y>=49&&y<=104))||
                        ((y>=76&&y<=77)&&(x>=126&&x<=164))||
                        (x>=184&&x<=186&&y>=49&&y<=104);
        wanted+=logo;
        const int noise=(x*17+y*43)%5-2;
        auto* p=colored.pixels.data()+(static_cast<size_t>(y)*320+x)*4;
        p[0]=static_cast<uint8_t>(logo?136:229-x*188/319+noise);
        p[1]=static_cast<uint8_t>(logo?42:42+y*26/159+noise);
        p[2]=static_cast<uint8_t>(logo?121:36+x*198/319+noise);
        p[3]=255;
    }
    const auto detail=ps::smartCutout(colored,{112,38,201,114},{},true);
    const auto wand=ps::selectSimilar(colored,{4,4},18,true,true);
    requireSmart(!detail.empty()&&wanted>0,"Colored gradient did not produce an output");
    int keptLines=0,droppedBackdrop=0,wandBackdrop=0;
    for(int y=0;y<160;++y)for(int x=0;x<320;++x) {
        const bool logo=((x==126||x==164)&&(y>=49&&y<=104))||
                        ((y>=76&&y<=77)&&(x>=126&&x<=164))||
                        (x>=184&&x<=186&&y>=49&&y<=104);
        const int i=y*320+x;
        if(logo)keptLines+=detail[i]>=128;
        else {droppedBackdrop+=detail[i]<=16;wandBackdrop+=wand[i]!=0;}
    }
    const int backdrop=320*160-wanted;
    std::cout<<"Color gradient thin lines retained "<<keptLines<<"/"<<wanted<<", backdrop removed "
        <<droppedBackdrop<<"/"<<backdrop<<" vs wand "<<wandBackdrop<<"/"<<backdrop<<"\n";
    requireSmart(keptLines>=0.90*wanted&&droppedBackdrop>=0.995*backdrop&&droppedBackdrop>wandBackdrop,
        "Smart cutout failed to improve a colored gradient or retain thin lines");

    std::cout<<"PASS: full-frame guidance, marked corrections, input transparency, gradients and soft edges\n";

    // A loose box on a large screenshot used to sample only background at
    // coarse cell centers and lose this one-pixel line completely.
    constexpr int largeWidth=2560,largeHeight=1440;
    ps::Bitmap large(largeWidth,largeHeight);
    for(int y=0;y<largeHeight;++y)for(int x=0;x<largeWidth;++x) {
        const bool line=x==1200&&y>=400&&y<=1000;
        const auto gray=static_cast<uint8_t>(70+x*144/largeWidth+y*18/largeHeight);
        auto* pixel=large.pixels.data()+(static_cast<size_t>(y)*largeWidth+x)*4;
        pixel[0]=line?30:gray;pixel[1]=line?80:gray;pixel[2]=line?150:gray;pixel[3]=255;
    }
    const RECT looseBox{200,200,2360,1240};
    const auto largeMask=ps::smartCutout(large,looseBox,{},true);
    requireSmart(!largeMask.empty(),"Coarse sampling lost all thin foreground evidence in a large box");
    int retained=0;size_t extra=0;
    for(int y=0;y<largeHeight;++y)for(int x=0;x<largeWidth;++x) {
        const auto alpha=largeMask[static_cast<size_t>(y)*largeWidth+x];
        if(x==1200&&y>=400&&y<=1000)retained+=alpha>=128;
        else extra+=alpha>16;
    }
    requireSmart(retained>=595&&extra<=12,"Large-box cutout lost the thin line or thickened coarse cells");
    const auto sharp=ps::smartCutout(large,looseBox,{},false);
    requireSmart(!sharp.empty()&&sharp[static_cast<size_t>(700)*largeWidth+1200]==255&&
        sharp[static_cast<size_t>(700)*largeWidth+1199]==0&&sharp[static_cast<size_t>(700)*largeWidth+1201]==0,
        "Disabling edge refinement expanded a one-pixel line into a coarse cell");
    const auto removed=ps::smartCutout(large,looseBox,{{false,3,{{1200,700}}}},true);
    requireSmart(!removed.empty()&&removed[static_cast<size_t>(700)*largeWidth+1200]==0,
        "Large-image detail recovery overrode a remove mark");
    std::cout<<"Large-box thin line retained "<<retained<<"/601, extra background pixels "<<extra<<'\n';

    // The foreground-training stride must not skip an isolated small detail.
    for(int y=400;y<=1000;++y) {
        const int x=1200;const auto gray=static_cast<uint8_t>(70+x*144/largeWidth+y*18/largeHeight);
        auto* pixel=large.pixels.data()+(static_cast<size_t>(y)*largeWidth+x)*4;
        pixel[0]=pixel[1]=pixel[2]=gray;
    }
    requireSmart(ps::smartCutout(large,looseBox,{},true).empty(),"Detail recovery invented foreground on an empty gradient");
    const size_t dot=static_cast<size_t>(700)*largeWidth+1200;
    large.pixels[dot*4]=30;large.pixels[dot*4+1]=80;large.pixels[dot*4+2]=150;
    const auto isolated=ps::smartCutout(large,looseBox,{},true);
    requireSmart(!isolated.empty()&&isolated[dot]>=128&&isolated[dot-1]==0&&isolated[dot+1]==0,
        "Sparse foreground fallback lost an isolated pixel or grew a coarse block");
    std::cout<<"PASS: large loose boxes, one-pixel detail, original-resolution background and remove-mark priority\n";
}
