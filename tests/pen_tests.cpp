#include <cstddef>
#include <cstdint>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/pen.hpp"

void run_pen_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;
    using visor::canvas::Color;
    using visor::draw::Pen;

    using TestCanvas = Canvas<SizeI{.w = 32, .h = 32}>;
    using OddCanvas = Canvas<SizeI{.w = 13, .h = 5}>;

    // plot lights exactly one pixel: pixel (3, 4) is bit 0x10 of row 4's first byte.
    TestCanvas single;
    const Pen pen{single, Color::On};
    pen.plot(PointI{3, 4});
    VISOR_CHECK_EQ(single.get((PointI{3, 4})), Color::On);
    VISOR_CHECK_EQ(single.pixels()[(4 * 4) + 0], std::uint8_t{0x10});

    // Out-of-bounds plots vanish silently, the buffer stays byte-identical.
    const TestCanvas before = single;
    pen.plot((PointI{-1, 0}));
    pen.plot((PointI{32, 0}));
    pen.plot((PointI{0, 32}));
    pen.plot((PointI{0, -1}));
    for (std::size_t i = 0; i < before.pixels().size(); ++i) {
        VISOR_CHECK_EQ(single.pixels()[i], before.pixels()[i]);
    }

    // fill paints the half-open area: lit pixels are [x, x+w) x [y, y+h).
    TestCanvas inside;
    const Pen on_brush{inside, Color::On};
    on_brush.fill((RectI{.pos = PointI{2, 3}, .size = SizeI{.w = 4, .h = 5}}));
    VISOR_CHECK_EQ(inside.get((PointI{2, 3})), Color::On);
    VISOR_CHECK_EQ(inside.get((PointI{5, 7})), Color::On);
    VISOR_CHECK_EQ(inside.get((PointI{1, 3})), Color::Off);
    VISOR_CHECK_EQ(inside.get((PointI{6, 7})), Color::Off);
    VISOR_CHECK_EQ(inside.get((PointI{2, 8})), Color::Off);

    // A band flush against the right edge paints through the last column.
    on_brush.fill((RectI{.pos = PointI{28, 0}, .size = SizeI{.w = 4, .h = 32}}));
    VISOR_CHECK_EQ(inside.get((PointI{28, 0})), Color::On);
    VISOR_CHECK_EQ(inside.get((PointI{31, 31})), Color::On);
    VISOR_CHECK_EQ(inside.get((PointI{27, 0})), Color::Off);

    // A negative-origin area clips to the canvas: exactly [0,20) x [0,20) lights up,
    // verified pixel by pixel, not only at the corners.
    TestCanvas clipped;
    const Pen clip_brush{clipped, Color::On};
    clip_brush.fill((RectI{.pos = PointI{-10, -10}, .size = SizeI{.w = 30, .h = 30}}));
    std::size_t lit_count = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool expect_lit = x < 20 && y < 20;
            VISOR_CHECK_EQ(clipped.get((PointI{x, y})), expect_lit ? Color::On : Color::Off);
            if (expect_lit) {
                ++lit_count;
            }
        }
    }
    VISOR_CHECK_EQ(lit_count, std::size_t{400});

    // A fully outside area and degenerate areas are no-ops.
    const TestCanvas untouched = clipped;
    clip_brush.fill((RectI{.pos = PointI{130, 10}, .size = SizeI{.w = 5, .h = 5}}));
    clip_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 0, .h = 5}}));
    clip_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 5, .h = 0}}));
    clip_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = -3, .h = 5}}));
    for (std::size_t i = 0; i < untouched.pixels().size(); ++i) {
        VISOR_CHECK_EQ(clipped.pixels()[i], untouched.pixels()[i]);
    }

    // A 3-wide fill starting at pixel 1 sets bits 0x40, 0x20 and 0x10 of the first byte.
    OddCanvas odd;
    const Pen odd_brush{odd, Color::On};
    odd_brush.fill((RectI{.pos = PointI{1, 0}, .size = SizeI{.w = 3, .h = 1}}));
    VISOR_CHECK_EQ(odd.pixels()[0], std::uint8_t{0x70});

    // Filling the whole canvas matches set_clear(On) byte for byte.
    TestCanvas whole;
    const Pen whole_brush{whole, Color::On};
    whole_brush.fill((RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 32, .h = 32}}));
    TestCanvas cleared;
    cleared.set_clear(Color::On);
    for (std::size_t i = 0; i < whole.pixels().size(); ++i) {
        VISOR_CHECK_EQ(whole.pixels()[i], cleared.pixels()[i]);
    }

    // An Off brush erases: the lit region goes dark, the rest stays lit.
    TestCanvas erase;
    erase.set_clear(Color::On);
    const Pen off_brush{erase, Color::Off};
    off_brush.fill((RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 8, .h = 8}}));
    VISOR_CHECK_EQ(erase.get((PointI{4, 4})), Color::Off);
    VISOR_CHECK_EQ(erase.get((PointI{11, 11})), Color::Off);
    VISOR_CHECK_EQ(erase.get((PointI{3, 4})), Color::On);
    VISOR_CHECK_EQ(erase.get((PointI{12, 11})), Color::On);
}
