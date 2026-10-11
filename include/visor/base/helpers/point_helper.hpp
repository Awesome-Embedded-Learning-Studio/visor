#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"

namespace visor::base {

// Yes, we can gain the other offsets
template <Arithmetic T>
[[nodiscard]] constexpr Point<T> offset(const Point<T>& p, const Size<T>& s) {
    return {p.x + s.w, p.y + s.h};
}

// The exclusive bottom-right corner reads as plain arithmetic: pos + size.
template <Arithmetic T>
[[nodiscard]] constexpr Point<T> operator+(const Point<T>& p, const Size<T>& s) {
    return offset(p, s);
}

// Yes, we can gain the other offsets
template <Arithmetic T>
[[nodiscard]] constexpr Point<T> operator-(const Point<T>& p, const Size<T>& s) {
    return {p.x - s.w, p.y - s.h};
}

} // namespace visor::base