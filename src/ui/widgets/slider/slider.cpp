#include "visor/ui/widgets/slider/slider.hpp"

#include <algorithm>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

Slider::Slider(base::RectI bounds) noexcept : Widget{bounds} {}

int Slider::value() const noexcept {
    return value_;
}

int Slider::max() const noexcept {
    return bounds().size.w - kHandleWidth;
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
    // A one-row track across the middle, the handle a dark block riding it:
    // the track says where, the handle says what.
    const int track_y = bounds().pos.y + (bounds().size.h / 2);
    painter.fill(base::RectI{.pos = base::PointI{bounds().pos.x, track_y},
                             .size = base::SizeI{.w = bounds().size.w, .h = 1}},
                 canvas::Color::On);
    painter.fill(base::RectI{.pos = base::PointI{bounds().pos.x + value_, track_y - 1},
                             .size = base::SizeI{.w = kHandleWidth, .h = 3}},
                 canvas::Color::Off);
}

} // namespace visor::ui
