#include "visor/ui/widgets/slider/slider.hpp"

#include <algorithm>
#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

Slider::Slider(base::RectI bounds) noexcept : Widget{bounds} {}

int Slider::value() const noexcept {
    return value_;
}

int Slider::max() const noexcept {
    return std::max(0, bounds().size.w - kHandleWidth);
}

void Slider::set_value(int value) noexcept {
    // Clamp in two named steps: a nested conditional would pack the range
    // check into a riddle.
    int clamped = value;
    clamped = std::min(clamped, max());
    clamped = std::max(clamped, 0);
    if (clamped == value_) {
        return; // same value, same pixels, no debt
    }
    value_ = clamped;
    invalidate(bounds());
    if (on_value_ != nullptr) {
        on_value_(*this, value_context_);
    }
}

void Slider::set_on_value(ValueHandler handler, void* context) noexcept {
    on_value_ = handler;
    value_context_ = context;
}

int Slider::value_at(int x) const noexcept {
    int raw = x - bounds().pos.x;
    raw = std::min(raw, max());
    return std::max(raw, 0);
}

void Slider::on_pointer_down(base::PointI at) noexcept {
    // The press grabs: the handle jumps to the press, then capture keeps the
    // moves coming however far the pointer wanders.
    dragging_ = true;
    set_value(value_at(at.x));
}

void Slider::on_pointer_move(base::PointI at) noexcept {
    if (dragging_) {
        set_value(value_at(at.x));
    }
}

void Slider::on_pointer_up([[maybe_unused]] base::PointI at) noexcept {
    dragging_ = false;
}

void Slider::on_draw(const Painter& painter) const noexcept {
    if (bounds().is_empty()) {
        return;
    }
    const auto fg = theme() != nullptr ? theme()->foreground : canvas::Color::On;
    const auto bg = theme() != nullptr ? theme()->background : canvas::Color::Off;
    const int track_y = bounds().pos.y + (bounds().size.h / 2);
    const int width = std::min(kHandleWidth, bounds().size.w);
    const int height = std::min(9, bounds().size.h);
    const int offset = std::clamp(value_, 0, max());
    painter.fill(
        base::RectI{.pos = {bounds().pos.x, track_y}, .size = {.w = bounds().size.w, .h = 1}}, fg);
    const base::RectI handle{
        .pos = {bounds().pos.x + offset, bounds().pos.y + ((bounds().size.h - height) / 2)},
        .size = {.w = width, .h = height},
    };
    painter.fill_rounded(
        handle, fg,
        CornerRadius{.top_left = 2, .top_right = 2, .bottom_right = 2, .bottom_left = 2});
    painter.fill_rounded(
        base::inset(handle, 1), bg,
        CornerRadius{.top_left = 1, .top_right = 1, .bottom_right = 1, .bottom_left = 1});
}

void Slider::on_key_down(std::uint32_t code) noexcept {
    if (code == host::kKeyLeft) {
        set_value(value_ - 1);
    } else if (code == host::kKeyRight) {
        set_value(value_ + 1);
    }
}

} // namespace visor::ui
