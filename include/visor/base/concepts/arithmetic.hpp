#pragma once

#include <concepts>
#include <type_traits>

namespace visor::base {
/**
 * @brief   Arithmetic means the type is a built-in arithmetic type, usable in coordinate math
 * @note    Bool is excluded: it satisfies std::is_arithmetic but has no meaning as a coordinate
 * @tparam  IsArithmetic the type to test
 */
template <typename IsArithmetic>
concept Arithmetic = std::is_arithmetic_v<IsArithmetic> && !std::same_as<IsArithmetic, bool>;

// The bool exclusion is a promise, so it is checked where the promise is made.
static_assert(!Arithmetic<bool>);
static_assert(Arithmetic<int> && Arithmetic<float> && Arithmetic<double>);

} // namespace visor::base
