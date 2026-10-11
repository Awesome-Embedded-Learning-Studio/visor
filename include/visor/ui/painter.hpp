#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"

namespace visor::ui {

/**
 * @brief Runtime drawing surface that widgets paint through.
 *
 * Widgets are runtime objects, while canvas types are sized at compile time, so
 * a widget cannot name the canvas it will eventually paint on: a template
 * parameter here would lock the whole UI layer to one screen size. Painter is
 * the seam between those two worlds: widgets see only this interface, and a
 * small adapter (one per raster profile) binds it to a concrete canvas. The two
 * verbs mirror the Pen vocabulary on purpose, so chapter five's clipping and
 * half-open rectangle semantics keep applying unchanged underneath.
 */
class Painter {
  public:
    /// Fills a half-open rectangle with one color; out-of-frame parts clip silently.
    virtual void fill(base::RectI area, canvas::Color ink) const noexcept = 0;
    /// Paints a single pixel; a point outside the frame is dropped silently.
    virtual void plot(base::PointI at, canvas::Color ink) const noexcept = 0;

  protected:
    // Non-virtual and protected: implementations are owned by their creator, never
    // deleted through a Painter pointer, so the class carries no vtable dtor cost.
    ~Painter() = default;
};

} // namespace visor::ui
