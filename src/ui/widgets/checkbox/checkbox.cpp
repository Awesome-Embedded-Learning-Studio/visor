#include "visor/ui/widgets/checkbox/checkbox.hpp"

#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"

namespace visor::ui {

bool Checkbox::checked() const noexcept {
    return checked_;
}

void Checkbox::on_pointer_down([[maybe_unused]] base::PointI at) noexcept {
    checked_ = !checked_;
    invalidate(bounds());
}

void Checkbox::on_key_down(std::uint32_t code) noexcept {
    // Activation is the keyboard's press: the two named keys flip the box
    // exactly as a pointer press does -- Space in its code-point form
    // included, a text host types 0x20 -- and every other code is none of
    // ours.
    if (code == host::kKeyEnter || code == host::kKeySpace || code == host::kCharSpace) {
        checked_ = !checked_;
        invalidate(bounds());
    }
}

void Checkbox::on_draw(const Painter& painter) const noexcept {
    // The scheme's two roles, or the hardcoded light polarity. The box stays
    // square: a two-inset box with a radius is a dot, not a checkbox.
    const canvas::Color fg = theme() != nullptr ? theme()->foreground : canvas::Color::On;
    const canvas::Color bg = theme() != nullptr ? theme()->background : canvas::Color::Off;
    // The box rides two pixels inside the bounds: one row for the focus ring
    // the tree paints after this, one lit margin between ring and box so the
    // two dark lines never merge into one thick border. A box smaller than the
    // insets turns empty, and the Pen's fill drops empty rectangles silently.
    const base::RectI box{
        .pos = base::PointI{bounds().pos.x + 2, bounds().pos.y + 2},
        .size = base::SizeI{.w = bounds().size.w - 4, .h = bounds().size.h - 4},
    };
    painter.fill(box, bg);
    if (!checked_) {
        const base::RectI inner{
            .pos = base::PointI{box.pos.x + 1, box.pos.y + 1},
            .size = base::SizeI{.w = box.size.w - 2, .h = box.size.h - 2},
        };
        painter.fill(inner, fg);
    }
}

} // namespace visor::ui
