#pragma once
#include "selection.h"

namespace ps {
struct CutoutStroke {
    bool keep;
    int radius;
    std::vector<POINT> points;
};

// Returns the foreground opacity in screenshot pixels, or an empty mask if
// there are insufficient samples to identify both foreground and background.
SelectionMask smartCutout(const Bitmap& image, RECT subject,
                          const std::vector<CutoutStroke>& strokes, bool refine);
}
