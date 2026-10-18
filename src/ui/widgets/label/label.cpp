#include "visor/ui/widgets/label/label.hpp"

#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

Label::Label(base::PointI origin, std::string_view text, const draw::MonoFont& font,
             canvas::ColorRole role, int scale) noexcept
    : Widget{base::RectI{.pos = origin, .size = draw::measure_text(font, text, scale)}},
      text_{text}, font_{font}, role_{role}, scale_{scale} {}

void Label::set_text(std::string_view text) noexcept {
    if (text == text_) {
        return; // Same words, same box, no debt.
    }
    text_ = text;
    // set_bounds keeps the origin and reports both boxes; measurement is the
    // only input to the size, so this one call is the whole resize story.
    set_bounds(measured_bounds(bounds().pos));
    invalidate(bounds());
}

std::string_view Label::text() const noexcept {
    return text_;
}

base::RectI Label::measured_bounds(base::PointI origin) const noexcept {
    return base::RectI{.pos = origin, .size = draw::measure_text(font_, text_, scale_)};
}

void Label::on_draw(const Painter& painter) const noexcept {
    draw_text(painter, bounds().pos, font_, text_, role_, scale_);
}

} // namespace visor::ui
