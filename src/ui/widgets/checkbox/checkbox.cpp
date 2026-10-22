#include "visor/ui/widgets/checkbox/checkbox.hpp"

#include <algorithm>
#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"

namespace visor::ui {

bool Checkbox::checked() const noexcept {
    return checked_;
}

void Checkbox::set_checked(bool on) noexcept {
    if (checked_ == on) {
        return;
    }
    checked_ = on;
    invalidate(bounds());
}

void Checkbox::on_pointer_down([[maybe_unused]] base::PointI at,
                               [[maybe_unused]] host::Modifiers modifiers) noexcept {
    checked_ = !checked_;
    invalidate(bounds());
}

void Checkbox::on_key_down(std::uint32_t code,
                           [[maybe_unused]] host::Modifiers modifiers) noexcept {
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
    const auto frame = canvas::ColorRole::Border;
    const auto ground = canvas::ColorRole::Background;
    const base::RectI box = base::inset(bounds(), 2);
    painter.fill_rounded(
        box, frame,
        CornerRadius{.top_left = 1, .top_right = 1, .bottom_right = 1, .bottom_left = 1});
    painter.fill(base::inset(box, 1), checked_ ? canvas::ColorRole::Accent : ground);
    if (!checked_ || box.size.w < 3 || box.size.h < 3) {
        return;
    }
    // Two integer strokes form a check, sized to the available inner square.
    const base::RectI mark = base::inset(box, 1);
    const int knee = mark.size.w / 3;
    for (int x = 0; x < mark.size.w; ++x) {
        const int y =
            x <= knee ? (mark.size.h / 2) +
                            (x * (mark.size.h - 1 - (mark.size.h / 2)) / std::max(1, knee))
                      : mark.size.h - 1 -
                            ((x - knee) * (mark.size.h - 1) / std::max(1, mark.size.w - 1 - knee));
        painter.plot(base::PointI{mark.pos.x + x, mark.pos.y + y}, canvas::ColorRole::OnAccent);
    }
}

} // namespace visor::ui
