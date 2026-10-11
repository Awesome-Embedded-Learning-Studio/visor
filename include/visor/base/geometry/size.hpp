#pragma once
#include "visor/base/concepts/arithmetic.hpp"

namespace visor::base {
template <Arithmetic T>
struct Size {
    T w{};
    T h{};
    constexpr bool operator==(const Size&) const = default;
};

using SizeI = Size<int>;

} // namespace visor::base
