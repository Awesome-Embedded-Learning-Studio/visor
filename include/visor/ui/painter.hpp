#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/theme.hpp"

#include <cstddef>
#include <cstdint>

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
    /// Fills a half-open rectangle with one color role; out-of-frame parts clip silently.
    virtual void fill(base::RectI area, canvas::ColorRole role) const noexcept = 0;
    /// Paints a single pixel in one color role; a point outside the frame is dropped silently.
    virtual void plot(base::PointI at, canvas::ColorRole role) const noexcept = 0;
    /// Blits a packed 1bpp bitmap: every lit source pixel becomes a scale x
    /// scale block of the role's ink, dark pixels paint nothing (this is ink
    /// transfer, not area overwrite — erasing around a bitmap is the caller's
    /// fill). Parts outside the frame clip silently. scale < 1 clamps to 1.
    /// The scale is explicit at every call site on purpose: a default argument
    /// on a virtual would bind to the static type's declaration, so the seam
    /// keeps none.
    virtual void draw_bitmap(base::PointI at, const draw::MonoBitmap& source,
                             canvas::ColorRole role, int scale) const noexcept = 0;
    /// Pastes a rectangle of XRGB8888 words (0xAARRGGBB values, stride_words
    /// apart) at the given point: content imagery, not a role. Each source
    /// word is its own ink, and the target's traits fold it -- a 565 canvas
    /// quantizes the channels, a mono canvas takes the polarity (any
    /// non-black word reads lit). Parts outside the frame clip silently.
    /// This is the verb a windowed program's pixels arrive on: the words are
    /// data handed over, never state shared with the painter.
    virtual void draw_image(base::PointI at, const std::uint32_t* words, int width, int height,
                            std::size_t stride_words) const noexcept = 0;
    /// Fills a rectangle with its corners rounded per radius: every row
    /// paints one span, the span's insets follow the corner discs (a pixel
    /// in a corner's zone stays dark when its corner point lies outside the
    /// quarter disc). Radius zero is the plain fill, an oversized radius
    /// clamps into a capsule, parts outside the frame clip silently.
    virtual void fill_rounded(base::RectI area, canvas::ColorRole role,
                              const CornerRadius& radius) const noexcept = 0;
    /// The raw-ink lane, one overload beside every role verb: a color value
    /// straight from data -- a terminal's SGR palette, decoded imagery --
    /// rides these without inventing a theme slot for it. Targets fold the
    /// ink through their own traits exactly as draw_image's words already
    /// do: a mono canvas takes the polarity, a 565 canvas quantizes.
    virtual void fill(base::RectI area, canvas::Ink ink) const noexcept = 0;
    /// Plots one pixel at a raw ink; same silent-drop stance as the role verb.
    virtual void plot(base::PointI at, canvas::Ink ink) const noexcept = 0;
    /// Blits a packed 1bpp bitmap in a raw ink; ink transfer, not area
    /// overwrite, same as the role verb.
    virtual void draw_bitmap(base::PointI at, const draw::MonoBitmap& source, canvas::Ink ink,
                             int scale) const noexcept = 0;
    /// The rounded fill in a raw ink; radius zero is the plain fill, same
    /// clamps as the role verb.
    virtual void fill_rounded(base::RectI area, canvas::Ink ink,
                              const CornerRadius& radius) const noexcept = 0;

  protected:
    // Non-virtual and protected: implementations are owned by their creator, never
    // deleted through a Painter pointer, so the class carries no vtable dtor cost.
    ~Painter() = default;
};

} // namespace visor::ui
