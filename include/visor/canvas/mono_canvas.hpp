#pragma once
#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace visor::canvas {

/// @brief Pixel x's bit position inside its byte, MSB first: pixel 0 rides the top
///        bit. The one home of the mono packing formula; hosts decode with it too.
constexpr std::uint8_t pixel_bit_position(std::size_t x) {
    return static_cast<std::uint8_t>(7 - (x % 8));
}
/**
 * @brief A fixed-capacity 1bpp drawing surface, sized by a compile-time SizeI.
 *
 * Row-major, MSB-first packing: pixel x of a row lives in byte x/8 at bit 0x80 >> (x%8),
 * each row takes (Width+7)/8 bytes, and the padding bits past the last pixel stay zero.
 * This is the canvas-internal format, not any display hardware's page format.
 *
 * Bit access rides the base::bit vocabulary: a byte is a BitMask word, a pixel is a
 * one-bit field at pixel_bit_position(x), so the position-to-value formula exists exactly
 * once in the whole library.
 */
template <base::SizeI CanvasSize>
class Canvas {
  public:
    static constexpr std::size_t kWidth = CanvasSize.w;
    static constexpr std::size_t kHeight = CanvasSize.h;
    static constexpr std::size_t kStrideBytes = (kWidth + 7) / 8;

    /// Sets every pixel to state. The padding bits past the last pixel stay zero.
    constexpr void set_clear(Color state) noexcept {
        bits_.fill(state == Color::On ? base::bit::ones<std::uint8_t>(8) : std::uint8_t{0});
        if constexpr (kWidth % 8 != 0) {
            // The tail byte's padding field: the bits below the last pixel stay zero.
            const base::bit::BitRange padding{
                .low = 0,
                .width = static_cast<std::uint8_t>(8 - (kWidth % 8)),
            };
            for (std::size_t row = 0; row < kHeight; ++row) {
                const std::size_t index = (row * kStrideBytes) + (kStrideBytes - 1);
                base::bit::BitMask<std::uint8_t> word{bits_[index]};
                word.deposit(padding, 0);
                bits_[index] = word.raw;
            }
        }
    }

    /// Writes one pixel. Out-of-bounds coordinates return false and touch nothing.
    [[nodiscard]] constexpr bool set(base::PointI p, Color value) noexcept {
        if (p.x < 0 || p.y < 0) {
            return false;
        }
        const auto x = static_cast<std::size_t>(p.x);
        const auto y = static_cast<std::size_t>(p.y);
        if (x >= kWidth || y >= kHeight) {
            return false;
        }
        const std::size_t index = (y * kStrideBytes) + (x / 8);
        base::bit::BitMask<std::uint8_t> word{bits_[index]};
        word.deposit(base::bit::bit_at(pixel_bit_position(x)), value == Color::On ? 1 : 0);
        bits_[index] = word.raw;
        return true;
    }

    /// Reads one pixel. Out-of-bounds coordinates read as Off.
    [[nodiscard]] constexpr Color get(base::PointI p) const noexcept {
        if (p.x < 0 || p.y < 0) {
            return Color::Off;
        }
        const auto x = static_cast<std::size_t>(p.x);
        const auto y = static_cast<std::size_t>(p.y);
        if (x >= kWidth || y >= kHeight) {
            return Color::Off;
        }
        const std::size_t index = (y * kStrideBytes) + (x / 8);
        const base::bit::BitMask<std::uint8_t> word{bits_[index]};
        const bool on = word.has(base::bit::mask_bit<std::uint8_t>(pixel_bit_position(x)));
        return on ? Color::On : Color::Off;
    }

    /// Raw rows for the host table's flush, kStrideBytes bytes per row, MSB first.
    [[nodiscard]] constexpr std::span<const std::uint8_t> pixels() const noexcept { return bits_; }
    [[nodiscard]] constexpr std::span<std::uint8_t> pixels() noexcept { return bits_; }

  private:
    std::array<std::uint8_t, kStrideBytes * kHeight> bits_{};
};
} // namespace visor::canvas
