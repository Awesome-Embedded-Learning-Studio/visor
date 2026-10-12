#include "testing.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/ssd1306/page_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::FlushRegion;
using visor::host::ssd1306::PageBuffer;

/// Builds a FlushRegion over packed rows the way a canvas submit would:
/// rect in screen coordinates, pixels at the top row, stride hop per row.
FlushRegion region_of(RectI rect, const std::uint8_t* pixels, std::size_t stride) noexcept {
    return FlushRegion{.rect = rect,
                       .pixels = pixels,
                       .stride_bytes = stride,
                       .format = visor::host::PixelFormat::Mono1};
}

void origin_pixel_lands_in_page0_bit0() {
    // The touchstone of the translation: pixel x=0 is the canvas row's top
    // bit (0x80) and the panel column's bottom bit (0x01). Both packings
    // disagree on every axis, and this one byte shows it.
    PageBuffer buffer;
    const std::array<std::uint8_t, 1> row{0x80};
    visor::host::ssd1306::write_region(
        buffer,
        region_of(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 8, .h = 1}}, row.data(), 1));
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 0)], std::uint8_t{0x01});
}

void diagonal_transposes_bits_into_the_column_byte() {
    // An 8x8 diagonal, one lit pixel per row: on the panel each column
    // byte holds exactly the pixel of its own column, so column c reads
    // 1<<c. The lit bit walks from 0x80 down the source rows and from 0x01
    // up the destination columns.
    PageBuffer buffer;
    const std::array<std::uint8_t, 8> rows{0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    visor::host::ssd1306::write_region(
        buffer,
        region_of(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 8, .h = 8}}, rows.data(), 1));
    for (std::size_t column = 0; column < 8; ++column) {
        VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, column)],
                       static_cast<std::uint8_t>(1U << column));
    }
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 8)], std::uint8_t{0x00});
}

void a_tall_region_spans_pages_at_byte_edges() {
    // Rows 7 and 8 are one pixel apart but two pages apart: y=7 is page 0's
    // top bit ... its bottom bit (0x80), y=8 is page 1's bottom bit (0x01).
    PageBuffer buffer;
    std::array<std::uint8_t, 16> rows{};
    rows.fill(0x80); // one lit pixel per row, column x=0
    visor::host::ssd1306::write_region(
        buffer,
        region_of(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 1, .h = 16}}, rows.data(), 1));
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 0)], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(1, 0)], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(2, 0)], std::uint8_t{0x00});
}

void partial_page_rows_leave_neighbour_bits_alone() {
    // Region y=5..20 over a buffer pre-filled 0xAA. Page 0 gets bits 5..7
    // set (0xEA), page 1 is fully covered (0xFF), page 2 gets bits 0..4
    // (0xBF). The untouched bits of pages 0 and 2 survive the write.
    PageBuffer buffer;
    buffer.fill(0xAA);
    std::array<std::uint8_t, 256> source{}; // 16 rows of a full 128px width, 16 bytes each
    source.fill(0xFF);
    visor::host::ssd1306::write_region(
        buffer,
        region_of(RectI{.pos = PointI{0, 5}, .size = SizeI{.w = 128, .h = 16}}, source.data(), 16));
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 0)], std::uint8_t{0xEA});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(1, 0)], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(2, 0)], std::uint8_t{0xBF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(3, 0)], std::uint8_t{0xAA});
}

void off_pixels_clear_and_outside_bits_stay() {
    // A three-pixel row at x=3..5 over a 0xFF buffer. The region is one row
    // tall, so it owns only bit 0 of each column byte: x=3 and x=4 write on
    // over an already-on bit, x=5 writes off and clears exactly that bit
    // (0xFE), and the seven rows below stay lit everywhere. x=2 outside the
    // region never hears about it (0xFF). Source row 0xC0: two lit pixels
    // then a dark one, MSB first.
    PageBuffer buffer;
    buffer.fill(0xFF);
    const std::array<std::uint8_t, 1> row{0xC0};
    visor::host::ssd1306::write_region(
        buffer,
        region_of(RectI{.pos = PointI{3, 0}, .size = SizeI{.w = 3, .h = 1}}, row.data(), 1));
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 2)], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 3)], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 4)], std::uint8_t{0xFF});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(0, 5)], std::uint8_t{0xFE});
}

void out_of_panel_coordinates_drop_silently() {
    // A 5x5 region at the panel's bottom-right corner: only x=126..127 and
    // y=62..63 exist, the rest of the rect names pixels the panel does not
    // have, and they vanish the way out-of-bounds canvas writes do.
    PageBuffer buffer;
    std::array<std::uint8_t, 5> source{};
    source.fill(0xF8);
    visor::host::ssd1306::write_region(
        buffer,
        region_of(RectI{.pos = PointI{126, 62}, .size = SizeI{.w = 5, .h = 5}}, source.data(), 1));
    VISOR_CHECK(buffer.get_pixel(126, 62));
    VISOR_CHECK(buffer.get_pixel(127, 63));
    VISOR_CHECK(!buffer.get_pixel(128, 62));
    VISOR_CHECK(!buffer.get_pixel(126, 64));
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(7, 126)], std::uint8_t{0xC0});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(7, 128 - 1)], std::uint8_t{0xC0});
    VISOR_CHECK_EQ(buffer.bytes()[PageBuffer::byte_index(6, 126)], std::uint8_t{0x00});
}

void pixel_round_trip_and_fill() {
    PageBuffer buffer;
    buffer.fill(0x5A);
    VISOR_CHECK_EQ(buffer.bytes()[0], std::uint8_t{0x5A});
    buffer.set_pixel(0, 0, true);
    buffer.set_pixel(127, 63, true);
    buffer.set_pixel(-1, 0, true); // silently ignored, both ends
    buffer.set_pixel(0, 64, true);
    VISOR_CHECK(buffer.get_pixel(0, 0));
    VISOR_CHECK(buffer.get_pixel(127, 63));
    VISOR_CHECK(!buffer.get_pixel(1, 0));
    VISOR_CHECK(!buffer.get_pixel(0, 63));
    buffer.set_pixel(0, 0, false);
    VISOR_CHECK(!buffer.get_pixel(0, 0));
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_ssd1306_tests() {
    origin_pixel_lands_in_page0_bit0();
    diagonal_transposes_bits_into_the_column_byte();
    a_tall_region_spans_pages_at_byte_edges();
    partial_page_rows_leave_neighbour_bits_alone();
    off_pixels_clear_and_outside_bits_stay();
    out_of_panel_coordinates_drop_silently();
    pixel_round_trip_and_fill();
}
