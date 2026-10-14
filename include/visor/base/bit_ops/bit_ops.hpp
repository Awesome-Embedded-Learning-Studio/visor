/**
 * @file    bit_ops.hpp
 * @brief   Named bit-level primitives over plain integers.
 *
 * Ported from Cinux base/include/cinux/bit_ops (2026-10-07): namespace and
 * naming follow visor rules.
 *
 * @author  Charliechen114514
 * @date    2026-10-07
 * @version 0.1
 * @since   0.1.0
 * @ingroup base_bit_ops
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace visor::base::bit {

// Masking and shifting with small positive constants is this vocabulary's
// sanctioned shape. Integer promotion hands a sub-int word -- and any signed
// caller's literal -- to the operator as int, so the signedness that shows up
// here is the promotion's doing, not an oversight to repair.
// NOLINTBEGIN(bugprone-signed-bitwise)

/// @brief Low nibble of any integer: the least significant 4 bits.
/// @since 0.1.0
/// @ingroup base_bit_ops
template <typename T>
constexpr T low_nibble(T value) {
    return value & 0xF;
}

/// @brief High nibble of any integer: bits 7..4.
/// @since 0.1.0
/// @ingroup base_bit_ops
template <typename T>
constexpr T high_nibble(T value) {
    return (value >> 4) & 0xF;
}

/// @brief Low byte of any integer: the least significant 8 bits.
/// @since 0.1.0
/// @ingroup base_bit_ops
template <typename T>
constexpr T low_byte(T value) {
    return value & 0xFF;
}

/// @brief High byte of any integer: bits 15..8.
/// @since 0.1.0
/// @ingroup base_bit_ops
template <typename T>
constexpr T high_byte(T value) {
    return (value >> 8) & 0xFF;
}

/// @brief The nibble starting at a given bit position.
/// @param[in] value   Integer to slice.
/// @param[in] shift   Starting bit (0, 4, 8, ...).
/// @return  Four bits of value beginning at shift.
/// @since 0.1.0
/// @ingroup base_bit_ops
template <typename T>
constexpr T nibble_at(T value, std::uint8_t shift) {
    return (value >> shift) & 0xF;
}

// NOLINTEND(bugprone-signed-bitwise)

} // namespace visor::base::bit
