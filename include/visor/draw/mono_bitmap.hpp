#pragma once

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/canvas/mono_canvas.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace visor::draw {

/**
 * @brief A read-only view over one packed 1bpp image: a glyph or a picture.
 *
 * The packing is the canvas one (row-major, MSB-first, (width+7)/8 bytes per
 * row, zero padding tails), which is also exactly what visor.fontgen emits,
 * so a generated header's kWidth, kHeight and kBits assemble into a view
 * with no copy and no repacking. Glyphs are just small bitmaps here: labels,
 * icons and the desktop picture of later tiers all consume this one shape.
 *
 * The view owns nothing; the storage belongs to the caller (usually the
 * generated header, whose lifetime is the program's).
 */
struct MonoBitmap {
    std::size_t width = 0;
    std::size_t height = 0;
    std::span<const std::uint8_t> rows;

    /// Bytes per row in the canvas packing: (width + 7) / 8.
    [[nodiscard]] constexpr std::size_t stride() const noexcept { return (width + 7) / 8; }

    /// Reads one pixel; out-of-range reads as off. The bit position rides
    /// canvas::pixel_bit_position, the one home of that formula.
    [[nodiscard]] constexpr bool pixel(std::size_t x, std::size_t y) const noexcept {
        if (x >= width || y >= height || rows.empty()) {
            return false;
        }
        const base::bit::BitMask<std::uint8_t> word{rows[(y * stride()) + (x / 8)]};
        return word.has(base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)));
    }
};

} // namespace visor::draw
