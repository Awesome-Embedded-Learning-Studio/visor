#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"

namespace visor::ui {

/**
 * @brief Runtime drawing surface that widgets paint through.
 *
 * Widgets are runtime objects, while canvas types are sized at compile time, so
 * a widget cannot name the canvas it will eventually paint on: a template
 * parameter here would lock the whole UI layer to one screen size. Painter is
 * the seam between those two worlds: widgets see only this interface, and a
 * small adapter (one per raster profile) binds it to a concrete canvas. The
 * verbs mirror the Pen vocabulary on purpose, so chapter five's clipping and
 * half-open rectangle semantics keep applying unchanged underneath.
 */
class Painter {
  public:
    /// Fills a half-open rectangle with one color; out-of-frame parts clip silently.
    virtual void fill(base::RectI area, canvas::Color ink) const noexcept = 0;
    /// Paints a single pixel; a point outside the frame is dropped silently.
    virtual void plot(base::PointI at, canvas::Color ink) const noexcept = 0;
    /// Blits a packed 1bpp bitmap: every lit source pixel becomes a scale x
    /// scale block of ink, dark pixels paint nothing (this is ink transfer,
    /// not area overwrite — erasing around a bitmap is the caller's fill).
    /// Parts outside the frame clip silently. scale < 1 clamps to 1. The
    /// scale is explicit at every call site on purpose: a default argument on
    /// a virtual would bind to the static type's declaration, so the seam
    /// keeps none.
    virtual void draw_bitmap(base::PointI at, const draw::MonoBitmap& source, canvas::Color ink,
                             int scale) const noexcept = 0;

  protected:
    // Non-virtual and protected: implementations are owned by their creator, never
    // deleted through a Painter pointer, so the class carries no vtable dtor cost.
    ~Painter() = default;
};

} // namespace visor::ui
