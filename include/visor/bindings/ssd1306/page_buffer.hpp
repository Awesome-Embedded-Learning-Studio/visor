#pragma once

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/host.hpp"
#include "visor/canvas/mono_canvas.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace visor::host::ssd1306 {

/**
 * @brief   The GDDRAM image of a 128x64 panel in the chip's own memory layout.
 *
 * SSD1306 display memory is page-major: 8 pages of 128 columns, one byte per
 * column covering 8 screen rows, and within that byte the LSB is the page's
 * top row. The canvas packing is the opposite shape on both axes — row-major
 * against page-major, MSB-first against LSB-up — so moving pixels across is
 * a real translation, never a memcpy. That translation is the one piece of
 * the OLED host that is pure logic, so it lives and is verified here, on the
 * PC, before any board is attached.
 *
 * The byte is the unit of transfer over I2C, which is why this layout, not
 * the canvas layout, is what the wire speaks.
 */
class PageBuffer {
  public:
    static constexpr std::size_t kWidth = 128;
    static constexpr std::size_t kHeight = 64;
    static constexpr std::size_t kPages = kHeight / 8;
    static constexpr std::size_t kBytes = kWidth * kPages;

    /// GDDRAM address of one column byte: pages are contiguous spans of rows.
    static constexpr std::size_t byte_index(std::size_t page, std::size_t column) {
        return (page * kWidth) + column;
    }

    /// Sets every byte to value, the buffer's own idle state.
    constexpr void fill(std::uint8_t value) noexcept { bytes_.fill(value); }

    /// Writes one pixel. Out-of-range coordinates skip silently, the same
    /// stance as Canvas::set: the host sees any rect a region may name, and
    /// the parts outside the panel simply do not exist.
    constexpr void set_pixel(int x, int y, bool on) noexcept {
        if (x < 0 || y < 0) {
            return;
        }
        const auto column = static_cast<std::size_t>(x);
        const auto row = static_cast<std::size_t>(y);
        if (column >= kWidth || row >= kHeight) {
            return;
        }
        // The one home of the panel's bit formula: within a column byte the
        // LSB is the top row of the page, so screen row y rides bit y%8.
        base::bit::BitMask<std::uint8_t> word{bytes_[byte_index(row / 8, column)]};
        word.deposit(base::bit::bit_at(static_cast<std::uint8_t>(row % 8)), on ? 1 : 0);
        bytes_[byte_index(row / 8, column)] = word.raw;
    }

    /// Reads one pixel. Out-of-range reads as off, same as the canvas.
    [[nodiscard]] constexpr bool get_pixel(int x, int y) const noexcept {
        if (x < 0 || y < 0) {
            return false;
        }
        const auto column = static_cast<std::size_t>(x);
        const auto row = static_cast<std::size_t>(y);
        if (column >= kWidth || row >= kHeight) {
            return false;
        }
        const base::bit::BitMask<std::uint8_t> word{bytes_[byte_index(row / 8, column)]};
        return word.has(base::bit::mask_bit<std::uint8_t>(static_cast<std::uint8_t>(row % 8)));
    }

    /// Raw GDDRAM bytes, pages in order, ready for the wire's data phase.
    [[nodiscard]] constexpr std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

  private:
    std::array<std::uint8_t, kBytes> bytes_{};
};

/**
 * @brief   Translates one FlushRegion's pixels into the page layout.
 *
 * region carries the canvas packing (row-major, MSB-first, stride_bytes hop
 * per row) with a half-open rect in screen coordinates; every in-range pixel
 * writes its GDDRAM bit, and off clears as well as on sets. Clearing is not
 * optional: strips replay the same object several times and background
 * restoration must erase what moved away.
 *
 * Bits whose rows fall outside the region stay untouched: one column byte
 * spans eight rows, a region's height may not, so neighbours keep their bits.
 *
 * @note    The source bit position rides canvas::pixel_bit_position, the one
 *          home of that formula; the destination bit position is the panel's
 *          own, and lives in PageBuffer. Both ends of the translation name
 *          their layout exactly once.
 */
constexpr void write_region(PageBuffer& buffer, const FlushRegion& region) noexcept {
    for (int row = 0; row < region.rect.size.h; ++row) {
        const int screen_y = region.rect.pos.y + row;
        // Negative rows drop out first; the panel-bound check then compares
        // the signed coordinate against the unsigned dimension the safe way.
        if (screen_y < 0 || std::cmp_greater_equal(screen_y, PageBuffer::kHeight)) {
            continue;
        }
        const auto source_row = static_cast<std::size_t>(row) * region.stride_bytes;
        for (int col = 0; col < region.rect.size.w; ++col) {
            const auto source_col = static_cast<std::size_t>(col);
            const base::bit::BitMask<std::uint8_t> word{
                region.pixels[source_row + (source_col / 8)]};
            const bool on =
                word.has(base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(source_col)));
            buffer.set_pixel(region.rect.pos.x + col, screen_y, on);
        }
    }
}

} // namespace visor::host::ssd1306
