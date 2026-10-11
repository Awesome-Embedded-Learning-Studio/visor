#pragma once

#include <concepts>

#include "visor/base/concepts/arithmetic.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"

namespace visor::base {
/**
 * @brief Axis-aligned rectangle on a half-open area: it covers [x, x+w) x [y, y+h).
 *        A width or height of zero or less means an empty rectangle.
 */
template <Arithmetic T>
struct Rect {
    Point<T> pos{};
    Size<T> size{};

    constexpr bool operator==(const Rect&) const = default;

    [[nodiscard]] constexpr bool is_empty() const { return size.w <= 0 || size.h <= 0; }
    [[nodiscard]] constexpr bool contains(const Point<T>& p) const {
        return p.x >= pos.x && p.x < pos.x + size.w && p.y >= pos.y && p.y < pos.y + size.h;
    }
};

using RectI = Rect<int>;
using RectF = Rect<float>;
using RectD = Rect<double>;

} // namespace visor::base
