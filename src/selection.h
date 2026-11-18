#pragma once
#include "image.h"
namespace ps {
using SelectionMask = std::vector<uint8_t>;
// 0 matches exact RGB, 1..99 use perceptual color distance with reduced
// lightness sensitivity for shadows, 100 accepts every nontransparent color.
// Contiguous mode follows four-connected pixels and, below tolerance 100,
// respects local color edges so similar adjacent panels do not merge.
SelectionMask selectSimilar(const Bitmap& image, POINT seed, int tolerance, bool contiguous);
// Sweep a circular brush along a segment, expanding the colors under the brush.
// The destination is updated in place; already-added/removed pixels are skipped.
bool brushSimilar(const Bitmap& image, SelectionMask& selection, POINT from, POINT to,
                  int radius, int tolerance, bool contiguous, bool subtract);
void combineSelection(SelectionMask& destination, const SelectionMask& source, bool subtract);
void invertSelection(SelectionMask& selection, const Bitmap& image);
// Preserve pixel positions when changing a crop rectangle. New pixels use fill.
SelectionMask reframeMask(const SelectionMask& mask, RECT from, RECT to, uint8_t fill=0);
// Bounding box of visible selected pixels, or all nontransparent pixels when
// no mask is supplied. Empty content returns an empty rectangle.
RECT contentBounds(const Bitmap& image, const SelectionMask& selection = {});
SelectionMask cutoutOpacity(const SelectionMask& selection, int width, int height, bool keep, bool soften);
// Preview is opaque. The checkerboard and blue selection tint are never exported.
Bitmap transparencyPreview(const Bitmap& image, const SelectionMask& selection = {});
} // namespace ps
