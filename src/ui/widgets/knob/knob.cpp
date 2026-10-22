#include "visor/ui/widgets/knob/knob.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::canvas::ColorRole;
using visor::ui::Painter;

/// The sine table's unit: one unit of sine is 1/4096 of a full swing.
constexpr int kSineScale = 4096;
/// The face starts at the lower-left and sweeps 270 degrees to the lower-right,
/// leaving the gap at six o'clock.
constexpr int kDialStartDegrees = 135;
constexpr int kDialSweepDegrees = 270;
/// The direction table's resolution: 256 directions around the circle.
constexpr int kAngleSteps = 256;
/// The face covers 270 of those steps; the remaining 64 are the gap.
constexpr int kFaceSteps = (kDialSweepDegrees * kAngleSteps) / 360;
/// The lower-left end's index in the direction table.
constexpr int kDialStartIndex = (kDialStartDegrees * kAngleSteps) / 360;
/// The gap's two halves, each clamped to its nearer end.
constexpr int kGapHalf = (kAngleSteps - kFaceSteps) / 2;

// Bhaskara I's sine approximation in fixed point: for d in [0, 180],
// sin(d) ~= 4d(180-d) / (40500 - d(180-d)). The quadrants fold in around the
// two halves, so the call costs a few integer multiplies and one divide --
// enough accuracy for a dial whose whole face is a few dozen pixels across.
constexpr int sine_degrees(int degrees) noexcept {
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    int sign = 1;
    if (d >= 180) {
        d -= 180;
        sign = -1;
    }
    if (d > 90) {
        d = 180 - d;
    }
    const int product = d * (180 - d);
    return sign * ((4 * product * kSineScale) / (40500 - product));
}

/// One screen-space direction, y pointing down: index 0 is east, 64 south,
/// 128 west, 192 north.
struct Direction {
    std::int16_t x;
    std::int16_t y;
};

constexpr std::array<Direction, kAngleSteps> make_directions() noexcept {
    std::array<Direction, kAngleSteps> table{};
    for (int i = 0; i < kAngleSteps; ++i) {
        const int degrees = (i * 360) / kAngleSteps;
        table[static_cast<std::size_t>(i)] = Direction{
            .x = static_cast<std::int16_t>(sine_degrees(degrees + 90)),
            .y = static_cast<std::int16_t>(sine_degrees(degrees)),
        };
    }
    return table;
}

inline constexpr auto kDirections = make_directions();

// The inverse of the table: the direction whose dot product with (dx, dy) is
// largest is the one with the smallest angle to the pointer. Twenty-five
// dozen integer comparisons do atan2's job without a libm dependency.
[[nodiscard]] int direction_index(int dx, int dy) noexcept {
    int best = 0;
    int best_score = (dx * kDirections[0].x) + (dy * kDirections[0].y);
    for (int i = 1; i < kAngleSteps; ++i) {
        const Direction& direction = kDirections[static_cast<std::size_t>(i)];
        const int score = (dx * direction.x) + (dy * direction.y);
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

// Bresenham with a two-pixel square brush: the pointer is the one stroke on
// the face that must stay visible over the ring's inner ground.
void paint_segment(const Painter& painter, PointI from, PointI to, ColorRole role) noexcept {
    int x = from.x;
    int y = from.y;
    int dx = to.x - x;
    int dy = to.y - y;
    const int step_x = dx < 0 ? -1 : 1;
    const int step_y = dy < 0 ? -1 : 1;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    const auto brush = [&painter, role](PointI at) {
        painter.plot(at, role);
        painter.plot(PointI{at.x + 1, at.y}, role);
        painter.plot(PointI{at.x, at.y + 1}, role);
        painter.plot(PointI{at.x + 1, at.y + 1}, role);
    };
    int error = dx - dy;
    while (true) {
        brush(PointI{x, y});
        if (x == to.x && y == to.y) {
            return;
        }
        const int twice = 2 * error;
        if (twice > -dy) {
            error -= dy;
            x += step_x;
        }
        if (twice < dx) {
            error += dx;
            y += step_y;
        }
    }
}

} // namespace

namespace visor::ui {

Knob::Knob(base::RectI bounds, int minimum, int maximum, int value) noexcept
    : Widget{bounds}, minimum_{minimum}, maximum_{std::max(minimum, maximum)},
      value_{std::clamp(value, minimum_, maximum_)} {}

int Knob::value() const noexcept {
    return value_;
}

int Knob::minimum() const noexcept {
    return minimum_;
}

int Knob::maximum() const noexcept {
    return maximum_;
}

void Knob::set_value(int value) noexcept {
    const int clamped = std::clamp(value, minimum_, maximum_);
    if (clamped == value_) {
        return; // same value, same pointer angle, no debt
    }
    value_ = clamped;
    invalidate(bounds());
    if (on_value_ != nullptr) {
        on_value_(*this, value_context_);
    }
}

void Knob::set_on_value(ValueHandler handler, void* context) noexcept {
    on_value_ = handler;
    value_context_ = context;
}

base::PointI Knob::center() const noexcept {
    return {
        bounds().pos.x + (bounds().size.w / 2),
        bounds().pos.y + (bounds().size.h / 2),
    };
}

int Knob::radius() const noexcept {
    return std::min(bounds().size.w, bounds().size.h) / 2;
}

void Knob::update_value(base::PointI at) noexcept {
    const PointI middle = center();
    const int dx = at.x - middle.x;
    const int dy = at.y - middle.y;
    if (dx == 0 && dy == 0) {
        return; // the pivot has no direction to point at
    }
    const int index = direction_index(dx, dy);
    // Steps clockwise from the lower-left end. The face is 192 steps; the
    // 90-degree gap after it parks at the end it is nearer, so a drag through
    // six o'clock clamps instead of teleporting to the other end.
    int steps = index - kDialStartIndex;
    if (steps < 0) {
        steps += kAngleSteps;
    }
    if (steps > kFaceSteps) {
        const int gap = steps - kFaceSteps;
        steps = gap <= kGapHalf ? kFaceSteps : 0;
    }
    const int span = maximum_ - minimum_;
    set_value(minimum_ + ((steps * span) / kFaceSteps));
}

void Knob::on_pointer_down(base::PointI at, [[maybe_unused]] host::Modifiers modifiers) noexcept {
    // The box is the hit area, so the corners grab too; only the exact pivot
    // has no direction. The press turns the dial at once: a click on the face
    // is a value, not the start of an invisible relative drag.
    dragging_ = true;
    update_value(at);
}

void Knob::on_pointer_move(base::PointI at) noexcept {
    if (dragging_) {
        update_value(at);
    }
}

void Knob::on_pointer_up([[maybe_unused]] base::PointI at) noexcept {
    dragging_ = false;
}

void Knob::on_key_down(std::uint32_t code, [[maybe_unused]] host::Modifiers modifiers) noexcept {
    if (code == host::kKeyLeft || code == host::kKeyDown) {
        set_value(value_ - 1);
    } else if (code == host::kKeyRight || code == host::kKeyUp) {
        set_value(value_ + 1);
    }
}

void Knob::on_draw(const Painter& painter) const noexcept {
    if (bounds().is_empty()) {
        return;
    }
    const ColorRole ground = ColorRole::Background;
    const ColorRole face = ColorRole::Foreground;
    const ColorRole pointer = ColorRole::Accent;
    painter.fill(bounds(), ground);
    const int r = radius();
    if (r < 4) {
        painter.fill(bounds(), face);
        return;
    }
    const PointI middle = center();
    const int side = r * 2;
    const RectI disc{
        .pos = {middle.x - r, middle.y - r},
        .size = {.w = side, .h = side},
    };
    // A ring, not a solid face: the outer disc takes the ink, the inner one
    // takes the ground back, and the pointer and ticks land on the ground.
    const int clamped_radius = std::min(r, 255);
    const auto outer_radius = static_cast<std::uint8_t>(clamped_radius);
    painter.fill_rounded(disc, face,
                         CornerRadius{
                             .top_left = outer_radius,
                             .top_right = outer_radius,
                             .bottom_right = outer_radius,
                             .bottom_left = outer_radius,
                         });
    const auto inner_radius = static_cast<std::uint8_t>(std::max(0, clamped_radius - 3));
    painter.fill_rounded(base::inset(disc, 3), ground,
                         CornerRadius{
                             .top_left = inner_radius,
                             .top_right = inner_radius,
                             .bottom_right = inner_radius,
                             .bottom_left = inner_radius,
                         });
    // Eleven ticks across the face, one pixel wide, one pixel inside the ring.
    if (r >= 8) {
        for (int i = 0; i <= 10; ++i) {
            const int angle = kDialStartDegrees + ((i * kDialSweepDegrees) / 10);
            const int cos = sine_degrees(angle + 90);
            const int sin = sine_degrees(angle);
            for (int reach = r - 7; reach <= r - 4; ++reach) {
                painter.plot(
                    PointI{
                        middle.x + ((reach * cos) / kSineScale),
                        middle.y + ((reach * sin) / kSineScale),
                    },
                    face);
            }
        }
    }
    // The pointer's angle: a one-point dial parks at the lower-left start.
    const int span = maximum_ - minimum_;
    const int angle = span <= 0
                          ? kDialStartDegrees
                          : kDialStartDegrees + (((value_ - minimum_) * kDialSweepDegrees) / span);
    const int cos = sine_degrees(angle + 90);
    const int sin = sine_degrees(angle);
    const int reach = std::max(1, r - 5);
    paint_segment(painter, middle,
                  PointI{
                      middle.x + ((reach * cos) / kSineScale),
                      middle.y + ((reach * sin) / kSineScale),
                  },
                  pointer);
    const int hub = r >= 4 ? 1 : 0;
    painter.fill(
        RectI{
            .pos = {middle.x - hub, middle.y - hub},
            .size = {.w = 1 + (2 * hub), .h = 1 + (2 * hub)},
        },
        pointer);
}

} // namespace visor::ui
