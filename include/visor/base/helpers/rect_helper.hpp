#pragma once

#include "visor/base/concepts/arithmetic.hpp"
#include "visor/base/geometry/rect.hpp"

#include <algorithm>

namespace visor::base {
template <Arithmetic T>
[[nodiscard]] constexpr Rect<T> intersect(const Rect<T>& a, const Rect<T>& b) {
    const T left = std::max(a.pos.x, b.pos.x);
    const T top = std::max(a.pos.y, b.pos.y);
    const T right = std::min(a.pos.x + a.size.w, b.pos.x + b.size.w);
    const T bottom = std::min(a.pos.y + a.size.h, b.pos.y + b.size.h);
    return {Point<T>{left, top}, Size<T>{std::max(T{}, right - left), std::max(T{}, bottom - top)}};
}

/// Shrinks each side by a nonnegative distance; exhausted dimensions stay
/// zero. Negative distances are treated as zero rather than expanding.
template <Arithmetic T>
[[nodiscard]] constexpr Rect<T> inset(Rect<T> area, T distance) noexcept {
    distance = std::max(T{}, distance);
    const auto shrink = [distance](T length) {
        T remaining = std::max(T{}, length);
        // Subtract separately so unsigned coordinates cannot wrap below zero.
        remaining -= std::min(remaining, distance);
        remaining -= std::min(remaining, distance);
        return remaining;
    };
    return {
        .pos = {area.pos.x + distance, area.pos.y + distance},
        .size = {.w = shrink(area.size.w), .h = shrink(area.size.h)},
    };
}

/// The smallest rectangle covering both operands. An empty operand still
/// participates by its corner point, so callers that merge real areas keep
/// empty rectangles out of the merge; dirty-region collection does exactly
/// that by refusing to record them.
template <Arithmetic T>
[[nodiscard]] constexpr Rect<T> bounding_box(const Rect<T>& a, const Rect<T>& b) {
    const T left = std::min(a.pos.x, b.pos.x);
    const T top = std::min(a.pos.y, b.pos.y);
    const T right = std::max(a.pos.x + a.size.w, b.pos.x + b.size.w);
    const T bottom = std::max(a.pos.y + a.size.h, b.pos.y + b.size.h);
    return {Point<T>{left, top}, Size<T>{right - left, bottom - top}};
}
} // namespace visor::base
