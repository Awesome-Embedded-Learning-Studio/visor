#pragma once

#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

/**
 * @brief Text on screen, sized by its own content.
 *
 * The first control that measures itself: the constructor takes an origin
 * (not a rectangle) and derives the bounds from the font's measurement of
 * the text, so a label always exactly wraps what it says. set_text
 * re-measures at the same origin and resizes the box, reporting both the
 * old and the new area as owed repaint.
 *
 * Both the text and the font are views over storage the caller owns (the
 * static storage of a literal, or a generated header whose lifetime is the
 * program's) -- the whitelist discipline for every Cinux-linked layer: no
 * std::string, no heap, the control states what it is shown. A mutator may
 * hand in temporaries only if the storage they point at outlives the label.
 * Not focusable: a label states things, it takes no input.
 */
class Label : public Widget {
  public:
    Label(base::PointI origin, std::string_view text, const draw::MonoFont& font,
          canvas::ColorRole role = canvas::ColorRole::Foreground, int scale = 1) noexcept;

    /// Re-measures and resizes at the same origin. An unchanged text
    /// reports nothing, matching set_visible's stance on no-op calls.
    void set_text(std::string_view text) noexcept;

    [[nodiscard]] std::string_view text() const noexcept;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    [[nodiscard]] base::RectI measured_bounds(base::PointI origin) const noexcept;

    std::string_view text_;
    draw::MonoFont font_;
    canvas::ColorRole role_;
    int scale_;
};

} // namespace visor::ui
