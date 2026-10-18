#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
// rect_helper.hpp provides intersect, found through ADL on the Rect arguments;
// include-cleaner cannot attribute ADL hits, so the keep pragma stands in.
#include "visor/base/helpers/rect_helper.hpp" // IWYU pragma: keep

namespace visor::draw {

/**
 * @brief A drawing hand bound to one canvas: carries the pixel state, provides the strokes.
 *
 * Templates on the canvas type and its pixel type, not on how the canvas is
 * parameterized, so a profile parameter added in the RGB565 chapter never touches
 * the Pen. Requirement on CanvasT: exposes kWidth, kHeight and set(PointI, Pixel).
 * A pen is meant to be short-lived, created per draw call beside the canvas it
 * borrows.
 */
template <typename CanvasT, typename Pixel>
class Pen {
  public:
    constexpr Pen(CanvasT& target, Pixel ink) noexcept : canvas_{target}, ink_{ink} {}

    /// Paints one pixel; an out-of-bounds plot vanishes silently. Const: the pen
    /// never changes, the canvas it points at does.
    constexpr void plot(base::PointI p) const noexcept {
        // set reports out-of-bounds with its return value; drawing ignores that on
        // purpose, stroke algorithms must not check bounds at every step.
        static_cast<void>(canvas_.set(p, ink_));
    }

    /// Fills the part of area that lands on the canvas, nothing outside.
    constexpr void fill(base::RectI area) const noexcept {
        const base::RectI visible =
            base::intersect(area, base::RectI{
                                      .pos = base::PointI{0, 0},
                                      .size =
                                          base::SizeI{
                                              .w = static_cast<int>(CanvasT::kWidth),
                                              .h = static_cast<int>(CanvasT::kHeight),
                                          },
                                  });
        if (visible.is_empty()) {
            return;
        }
        const int right = visible.pos.x + visible.size.w;
        const int bottom = visible.pos.y + visible.size.h;
        for (int y = visible.pos.y; y < bottom; ++y) {
            for (int x = visible.pos.x; x < right; ++x) {
                plot(base::PointI{x, y});
            }
        }
    }

  private:
    CanvasT& canvas_;
    Pixel ink_;
};

} // namespace visor::draw
