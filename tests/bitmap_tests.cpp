#include "testing.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/canvas_painter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::ColorRole;
using visor::draw::MonoBitmap;
using visor::ui::CanvasPainter;

// Hand-written in the exact shape visor.fontgen emits (B7's sample image, the
// 11x5 chevron): same member names, same packing, same comments. The
// assembly exercised here is the assembly a generated header goes through.
namespace assets::chevron {
inline constexpr std::size_t kWidth = 11;
inline constexpr std::size_t kHeight = 5;
inline constexpr std::size_t kStrideBytes = 2;
// Verbatim fontgen output shape: the generated header emits a plain array,
// and this case exercises that exact shape.
// NOLINTNEXTLINE(modernize-avoid-c-arrays)
inline constexpr std::uint8_t kBits[] = {
    0xf8, 0x00, // row 0
    0x04, 0x00, // row 1
    0x02, 0x00, // row 2
    0x01, 0xe0, // row 3
    0x00, 0x00, // row 4
};
} // namespace assets::chevron

// A generated header is a view's storage: the members line up without any
// repacking, and the emitted kStrideBytes must equal the derived one.
constexpr MonoBitmap kChevron{
    .width = assets::chevron::kWidth,
    .height = assets::chevron::kHeight,
    .rows = assets::chevron::kBits,
};
static_assert(assets::chevron::kStrideBytes == kChevron.stride());
static_assert(kChevron.pixel(0, 0));   // row 0's first lit pixel, MSB side
static_assert(!kChevron.pixel(10, 0)); // row 0's last pixel is padding-dark
static_assert(kChevron.pixel(8, 3));   // row 3, second byte's first pixel
static_assert(!kChevron.pixel(11, 0)); // out of range reads as dark

// 8x8 diagonal, one lit pixel per row: the classic walking-bit pattern. The
// row array is a named constant the view points at — a view over a temporary
// would dangle before main even starts.
constexpr std::array<std::uint8_t, 8> kDiagonalRows{0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
constexpr MonoBitmap kDiagonal{
    .width = 8,
    .height = 8,
    .rows = kDiagonalRows,
};

void diagonal_at_scale_one_lands_pixel_per_bit() {
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    const CanvasPainter painter{canvas};
    painter.draw_bitmap(PointI{2, 3}, kDiagonal, ColorRole::Foreground, 1);
    for (std::size_t step = 0; step < 8; ++step) {
        VISOR_CHECK(canvas.get(PointI{2 + static_cast<int>(step), 3 + static_cast<int>(step)}) ==
                    true);
    }
    VISOR_CHECK(canvas.get(PointI{3, 3}) == false);
    VISOR_CHECK(canvas.get(PointI{2, 4}) == false);
    VISOR_CHECK(canvas.get(PointI{10, 10}) == false);
}

void scale_two_grows_one_bit_into_a_block() {
    // Every lit source pixel becomes a 2x2 block at twice the coordinate.
    // Neighbour blocks stay disjoint (origins two apart), so a diagonal at
    // scale two lights exactly four pixels per step and leaves the off-diagonal
    // lanes dark.
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    const CanvasPainter painter{canvas};
    painter.draw_bitmap(PointI{0, 0}, kDiagonal, ColorRole::Foreground, 2);
    for (std::size_t step = 0; step < 8; ++step) {
        const int x = 2 * static_cast<int>(step);
        const int y = x;
        VISOR_CHECK(canvas.get(PointI{x, y}) == true);
        VISOR_CHECK(canvas.get(PointI{x + 1, y}) == true);
        VISOR_CHECK(canvas.get(PointI{x, y + 1}) == true);
        VISOR_CHECK(canvas.get(PointI{x + 1, y + 1}) == true);
    }
    // Between two diagonal blocks: x=1,y=2 belongs to no lit source pixel.
    VISOR_CHECK(canvas.get(PointI{1, 2}) == false);
    VISOR_CHECK(canvas.get(PointI{2, 1}) == false);
}

void dark_pixels_paint_nothing() {
    // Ink transfer, not area overwrite: pre-lit canvas stays lit wherever the
    // bitmap is dark, so erasing around a bitmap is the caller's fill.
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    canvas.set_clear(true);
    const CanvasPainter painter{canvas};
    painter.draw_bitmap(PointI{4, 4}, kDiagonal, ColorRole::Background, 1);
    // The dark majority of the glyph box keeps the canvas ink: (6, 4) and
    // (11, 4) sit inside the box but off the diagonal.
    VISOR_CHECK(canvas.get(PointI{5, 4}) == true);
    VISOR_CHECK(canvas.get(PointI{4, 5}) == true);
    VISOR_CHECK(canvas.get(PointI{6, 4}) == true);
    VISOR_CHECK(canvas.get(PointI{11, 4}) == true);
    // Exactly the diagonal carries the stroke's ink.
    for (std::size_t step = 0; step < 8; ++step) {
        VISOR_CHECK(canvas.get(PointI{4 + static_cast<int>(step), 4 + static_cast<int>(step)}) ==
                    false);
    }
}

void canvas_edge_clips_silently() {
    // An 8x8 glyph at (14, 14) on a 16x16 canvas: only the 2x2 corner of the
    // glyph fits, the rest names pixels the canvas does not have.
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    const CanvasPainter painter{canvas};
    painter.draw_bitmap(PointI{14, 14}, kDiagonal, ColorRole::Foreground, 1);
    VISOR_CHECK(canvas.get(PointI{14, 14}) == true);
    VISOR_CHECK(canvas.get(PointI{15, 15}) == true);
    VISOR_CHECK(canvas.get(PointI{15, 14}) == false);
    VISOR_CHECK(canvas.get(PointI{14, 15}) == false);
}

void window_translates_and_clips() {
    // Chapter eleven's window: the buffer stores scene rect (2, 2, 8, 8); a
    // glyph drawn at scene (5, 4) lands shifted by the window origin, and the
    // parts the window does not store vanish.
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    const RectI window{.pos = PointI{2, 2}, .size = SizeI{.w = 8, .h = 8}};
    const CanvasPainter painter{canvas, window};
    painter.draw_bitmap(PointI{5, 4}, kDiagonal, ColorRole::Foreground, 1);
    // Scene (5,4) is buffer (3,2): diagonal steps land at buffer
    // (3+step, 2+step). The window spans scene x in [2,10), so of the eight
    // steps only five fit (x = 5..9); steps five to seven name columns the
    // window does not store.
    for (std::size_t step = 0; step < 5; ++step) {
        VISOR_CHECK(canvas.get(PointI{3 + static_cast<int>(step), 2 + static_cast<int>(step)}) ==
                    true);
    }
    VISOR_CHECK(canvas.get(PointI{8, 7}) == false);
    VISOR_CHECK(canvas.get(PointI{9, 8}) == false);
    VISOR_CHECK(canvas.get(PointI{10, 9}) == false);
    // And nothing leaked into the untouched rows below the window.
    VISOR_CHECK(canvas.get(PointI{3, 10}) == false);
}

void strip_replay_matches_full_frame() {
    // The same glyph scene painted once whole and once strip-by-strip: strip
    // replays hit a glyph several times (at scale two even inside one strip),
    // and the two frames must agree pixel for pixel.
    const auto paint_scene = [](const auto& painter) {
        painter.draw_bitmap(PointI{3, 2}, kChevron, ColorRole::Foreground, 1);
        painter.draw_bitmap(PointI{5, 8}, kDiagonal, ColorRole::Foreground, 2);
        painter.draw_bitmap(PointI{0, 0}, kChevron, ColorRole::Background, 1);
    };
    Canvas<SizeI{.w = 16, .h = 16}> whole;
    paint_scene(CanvasPainter<Canvas<SizeI{.w = 16, .h = 16}>>{whole});

    Canvas<SizeI{.w = 16, .h = 16}> replayed;
    for (int top = 0; top < 16; top += 6) {
        Canvas<SizeI{.w = 16, .h = 6}> strip;
        const RectI window{.pos = PointI{0, top}, .size = SizeI{.w = 16, .h = 6}};
        paint_scene(CanvasPainter<Canvas<SizeI{.w = 16, .h = 6}>>{strip, window});
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < 16; ++x) {
                static_cast<void>(replayed.set(PointI{x, top + y}, strip.get(PointI{x, y})));
            }
        }
    }
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            VISOR_CHECK(whole.get(PointI{x, y}) == replayed.get(PointI{x, y}));
        }
    }
}

void clamp_and_default_scale() {
    // The declared default is scale one, and a nonsensical scale below one
    // clamps to one rather than painting nothing or dividing by anything.
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    const CanvasPainter painter{canvas};
    painter.draw_bitmap(PointI{0, 0}, kDiagonal, ColorRole::Foreground, 0);
    VISOR_CHECK(canvas.get(PointI{0, 0}) == true);
    VISOR_CHECK(canvas.get(PointI{1, 0}) == false);
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_bitmap_tests() {
    diagonal_at_scale_one_lands_pixel_per_bit();
    scale_two_grows_one_bit_into_a_block();
    dark_pixels_paint_nothing();
    canvas_edge_clips_silently();
    window_translates_and_clips();
    strip_replay_matches_full_frame();
    clamp_and_default_scale();
}
