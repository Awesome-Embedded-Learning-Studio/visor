#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/pen.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

/**
 * @brief Binds the runtime Painter interface to one concrete canvas type.
 *
 * The template half of the seam: a CanvasPainter borrows a canvas, builds a Pen
 * per stroke and forwards every call, so clipping and the half-open rectangle
 * semantics stay chapter-five property. Widgets never see this type; only the
 * code that owns a canvas does -- a run loop builds one per frame and hands it
 * to the tree as a plain Painter reference.
 */
template <typename CanvasT>
class CanvasPainter final : public Painter {
  public:
    explicit CanvasPainter(CanvasT& canvas) noexcept : canvas_{canvas} {}

    void fill(base::RectI area, canvas::Color ink) const noexcept override {
        const draw::Pen<CanvasT, canvas::Color> pen{canvas_, ink};
        pen.fill(area);
    }

    void plot(base::PointI at, canvas::Color ink) const noexcept override {
        const draw::Pen<CanvasT, canvas::Color> pen{canvas_, ink};
        pen.plot(at);
    }

  private:
    CanvasT& canvas_;
};

} // namespace visor::ui
