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
} // namespace visor::base
