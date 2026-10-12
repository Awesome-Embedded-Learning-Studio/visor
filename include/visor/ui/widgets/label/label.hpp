#pragma once

#include <string>
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
 * The text is stored by value -- it is the label's state, and mutators may
 * hand in temporaries -- while the font is a view over storage the caller
 * owns (usually a generated header, whose lifetime is the program's). Not
 * focusable: a label states things, it takes no input.
 */
class Label : public Widget {
  public:
    /// Measured bounds at the origin. Allocation failure terminates (the
    /// tree runs with exceptions off); the same stance the tool takes.
    Label(base::PointI origin, std::string_view text, const draw::MonoFont& font, canvas::Color ink,
          int scale = 1) noexcept;

    /// Re-measures and resizes at the same origin. An unchanged text
    /// reports nothing, matching set_visible's stance on no-op calls.
    void set_text(std::string_view text) noexcept;

    [[nodiscard]] const std::string& text() const noexcept;

  protected:
    void on_draw(const Painter& painter) const noexcept override;

  private:
    [[nodiscard]] base::RectI measured_bounds(base::PointI origin) const noexcept;

    std::string text_;
    draw::MonoFont font_;
    canvas::Color ink_;
    int scale_;
};

} // namespace visor::ui
