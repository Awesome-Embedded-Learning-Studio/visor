#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/text.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::Color;
using visor::draw::measure_text;
using visor::draw::MonoFont;
using visor::draw::TextLayout;
using visor::ui::draw_text;

// Hand-written in the exact shape visor.fontgen's font mode emits: same
// member names, same packing, same comments. Three glyphs with deliberate
// irregularities -- an advance wider than the bitmap (the one-pixel gap a
// strip cut bakes into its cells) and a five-wide CJK glyph that only a
// three-byte UTF-8 sequence can name -- so the case exercises the shape, not
// just a row of identical cells.
namespace assets::demo_font {
inline constexpr std::size_t kLineHeight = 6;
// NOLINTNEXTLINE(modernize-avoid-c-arrays) -- verbatim fontgen output shape.
inline constexpr std::uint8_t kBits[] = {
    0x60, // U+0041 'A' row 0
    0x90, // U+0041 'A' row 1
    0xf0, // U+0041 'A' row 2
    0x90, // U+0041 'A' row 3
    0x90, // U+0041 'A' row 4
    0xe0, // U+0042 'B' row 0
    0x90, // U+0042 'B' row 1
    0xe0, // U+0042 'B' row 2
    0x90, // U+0042 'B' row 3
    0xe0, // U+0042 'B' row 4
    0x20, // U+4e2d row 0
    0xd8, // U+4e2d row 1
    0xd8, // U+4e2d row 2
    0xf8, // U+4e2d row 3
    0x20, // U+4e2d row 4
};
// NOLINTNEXTLINE(modernize-avoid-c-arrays) -- verbatim fontgen output shape.
inline constexpr visor::draw::GlyphEntry kGlyphs[] = {
    {.codepoint = 0x41,
     .width = 4,
     .height = 5,
     .byte_offset = 0,
     .advance = 5,
     .offset_x = 0,
     .offset_y = 0},
    {.codepoint = 0x42,
     .width = 4,
     .height = 5,
     .byte_offset = 5,
     .advance = 5,
     .offset_x = 0,
     .offset_y = 0},
    {.codepoint = 0x4e2d,
     .width = 5,
     .height = 5,
     .byte_offset = 10,
     .advance = 6,
     .offset_x = 0,
     .offset_y = 0},
};
} // namespace assets::demo_font

constexpr MonoFont kDemoFont{
    .line_height = assets::demo_font::kLineHeight,
    .glyphs = assets::demo_font::kGlyphs,
    .bits = assets::demo_font::kBits,
};

// The generated header is the view's storage: members line up without
// repacking, and lookups work at compile time, where a consumer's own
// static_asserts would live.
static_assert(kDemoFont.glyph(0x41) != nullptr);
static_assert(kDemoFont.glyph(0x41)->advance == 5);
// Last entry (the binary search edge): a hit is proven by reading through
// it -- GCC reads a folded hit compared against null as always-true
// (-Waddress), and the deref says the same thing without the argument.
static_assert(kDemoFont.glyph(0x4e2d)->advance == 6);
// The miss gets a name first: comparing the call's result straight against
// nullptr trips the same warning on the inlined pointer arithmetic.
constexpr const visor::draw::GlyphEntry* kNoC = kDemoFont.glyph(0x43);
static_assert(kNoC == nullptr);
static_assert(kDemoFont.bitmap(*kDemoFont.glyph(0x4e2d)).pixel(2, 0));

// Measurement is constexpr for the same reason: "A" is advance 5 wide, not
// bitmap 4, and an empty string still occupies one line of the grid.
static_assert(measure_text(kDemoFont, "A", 1) == SizeI{.w = 5, .h = 6});
static_assert(measure_text(kDemoFont, "AB", 1) == SizeI{.w = 10, .h = 6});
static_assert(measure_text(kDemoFont, "", 1) == SizeI{.w = 0, .h = 6});

void glyph_lookup_finds_and_misses() {
    VISOR_CHECK_EQ(kDemoFont.glyph(0x42)->advance, std::size_t{5});
    VISOR_CHECK_EQ(kDemoFont.glyph(0x4e2d)->width, std::size_t{5});
    const visor::draw::GlyphEntry* const nobody = nullptr;
    VISOR_CHECK_EQ(kDemoFont.glyph(0x5a), nobody); // 'Z' is not in this font
}

void bitmap_view_reads_glyph_bits() {
    const auto* const glyph = kDemoFont.glyph(0x4e2d);
    VISOR_CHECK(glyph != nullptr);
    const auto view = kDemoFont.bitmap(*glyph);
    VISOR_CHECK(view.pixel(2, 0)); // row 0's single stroke
    VISOR_CHECK(view.pixel(0, 1)); // row 1 lights all but the center column
    VISOR_CHECK(view.pixel(4, 1));
    VISOR_CHECK(!view.pixel(2, 1));
    VISOR_CHECK(view.pixel(2, 4));
    VISOR_CHECK(!view.pixel(5, 0)); // out of range reads as dark
}

void measure_reports_pen_end_not_ink_extent() {
    // Width is where the pen stops after the last glyph, so the one-pixel
    // gap an advance carries past the bitmap counts; a font whose advances
    // equalled its bitmap widths would measure "AB" at 8, not 10.
    VISOR_CHECK_EQ(measure_text(kDemoFont, "A", 1), (SizeI{.w = 5, .h = 6}));
    VISOR_CHECK_EQ(measure_text(kDemoFont, "AB", 1), (SizeI{.w = 10, .h = 6}));
    VISOR_CHECK_EQ(measure_text(kDemoFont, "A\xe4\xb8\xad", 1), (SizeI{.w = 11, .h = 6}));
}

void measure_counts_the_line_grid() {
    // Height is line_count * line_height, every newline included; the empty
    // string is one empty line, and a lone newline makes two of them.
    VISOR_CHECK_EQ(measure_text(kDemoFont, "A\nB", 1), (SizeI{.w = 5, .h = 12}));
    VISOR_CHECK_EQ(measure_text(kDemoFont, "\n", 1), (SizeI{.w = 0, .h = 12}));
    VISOR_CHECK_EQ(measure_text(kDemoFont, "", 1), (SizeI{.w = 0, .h = 6}));
}

void measure_skips_missing_glyphs() {
    // 'Z' has no glyph here and an invalid byte decodes to U+FFFD, which has
    // none either: both are skipped without ink and without advance.
    VISOR_CHECK_EQ(measure_text(kDemoFont, "AZ", 1), (SizeI{.w = 5, .h = 6}));
    VISOR_CHECK_EQ(measure_text(kDemoFont,
                                "\x80"
                                "A",
                                1),
                   (SizeI{.w = 5, .h = 6}));
}

void measure_scales_and_clamps() {
    VISOR_CHECK_EQ(measure_text(kDemoFont, "A\xe4\xb8\xad", 2), (SizeI{.w = 22, .h = 12}));
    VISOR_CHECK_EQ(measure_text(kDemoFont, "AB", 0), (SizeI{.w = 10, .h = 6})); // clamps to 1
}

void layout_places_glyphs_on_the_line_grid() {
    TextLayout layout{kDemoFont, "A\n\xe4\xb8\xad", 1};
    const auto first = layout.next();
    if (!first.has_value()) {
        VISOR_CHECK(false); // the walk owes a placement for A
        return;
    }
    VISOR_CHECK_EQ(first->pos, (PointI{0, 0}));
    VISOR_CHECK_EQ(first->glyph->codepoint, std::uint32_t{0x41});
    VISOR_CHECK_EQ(layout.pen_x(), 5); // the pen sits past A's advance
    const auto second = layout.next();
    if (!second.has_value()) {
        VISOR_CHECK(false); // and one more for the CJK glyph after the newline
        return;
    }
    VISOR_CHECK_EQ(second->pos, (PointI{0, 6})); // newline moved to the line grid
    VISOR_CHECK_EQ(second->glyph->codepoint, std::uint32_t{0x4e2d});
    VISOR_CHECK_EQ(layout.pen_x(), 6);
    VISOR_CHECK(!layout.next().has_value());
    VISOR_CHECK_EQ(layout.line_count(), std::size_t{2});

    TextLayout same_line{kDemoFont, "A\xe4\xb8\xad", 1};
    VISOR_CHECK(same_line.next().has_value());
    const auto after_wide = same_line.next();
    if (!after_wide.has_value()) {
        VISOR_CHECK(false); // same walk, second glyph on the same line
        return;
    }
    VISOR_CHECK_EQ(after_wide->pos, (PointI{5, 0})); // pen after A's advance
    VISOR_CHECK_EQ(same_line.line_count(), std::size_t{1});
}

void draw_text_paints_each_glyph_at_its_pen() {
    Canvas<SizeI{.w = 24, .h = 16}> canvas;
    canvas.set_clear(Color::Off);
    const visor::ui::CanvasPainter painter{canvas};
    draw_text(painter, PointI{3, 2}, kDemoFont, "AB", Color::On, 1);
    // A's row 0 is 0x60: x1 and x2 lit, so at origin (3,2) the pair (4,2)
    // and (5,2) carries ink and the cell's own first column stays dark.
    VISOR_CHECK(canvas.get(PointI{4, 2}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{5, 2}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{3, 2}) == Color::Off);
    // A's row 2 is the full 0xf0 bar.
    for (int x = 3; x < 7; ++x) {
        VISOR_CHECK(canvas.get(PointI{x, 4}) == Color::On);
    }
    // B starts at the pen past A: x = 3 + 5 = 8. Row 0 is 0xe0, three lit.
    VISOR_CHECK(canvas.get(PointI{8, 2}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{10, 2}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{11, 2}) == Color::Off);
    // B's row 1 is 0x90: columns 0 and 3 of the glyph.
    VISOR_CHECK(canvas.get(PointI{8, 3}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{11, 3}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{9, 3}) == Color::Off);
}

void draw_text_advances_lines_on_newline() {
    Canvas<SizeI{.w = 24, .h = 16}> canvas;
    canvas.set_clear(Color::Off);
    const visor::ui::CanvasPainter painter{canvas};
    draw_text(painter, PointI{0, 0}, kDemoFont, "A\nB", Color::On, 1);
    // B sits on the second line, six rows down: line_height 6 from origin 0.
    VISOR_CHECK(canvas.get(PointI{0, 6}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{2, 6}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{3, 6}) == Color::Off);
    // Nothing from B leaked back into the first line's tail columns.
    VISOR_CHECK(canvas.get(PointI{4, 6}) == Color::Off);
}

void draw_text_scales_blocks() {
    Canvas<SizeI{.w = 16, .h = 16}> canvas;
    canvas.set_clear(Color::Off);
    const visor::ui::CanvasPainter painter{canvas};
    draw_text(painter, PointI{0, 0}, kDemoFont, "A", Color::On, 2);
    // A's row 0 lights x1: a 2x2 block at (2,0). Row 2 lights x0: block at (0,4).
    VISOR_CHECK(canvas.get(PointI{2, 0}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{3, 1}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{0, 4}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{1, 5}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{0, 0}) == Color::Off);
    VISOR_CHECK(canvas.get(PointI{1, 1}) == Color::Off); // A's (1,1) is dark at 0x90
}

void draw_text_matches_strip_replay() {
    // The same text painted once whole and once strip-by-strip: a glyph
    // straddling a strip boundary is replayed across strips, and the frames
    // must agree pixel for pixel.
    const auto paint_scene = [](const auto& painter) {
        draw_text(painter, PointI{1, 1}, kDemoFont, "A\n\xe4\xb8\xad", Color::On, 1);
    };
    Canvas<SizeI{.w = 24, .h = 16}> whole;
    whole.set_clear(Color::Off);
    paint_scene(visor::ui::CanvasPainter<Canvas<SizeI{.w = 24, .h = 16}>>{whole});

    Canvas<SizeI{.w = 24, .h = 16}> replayed;
    replayed.set_clear(Color::Off);
    for (int top = 0; top < 16; top += 6) {
        const int height = top + 6 <= 16 ? 6 : 16 - top;
        const RectI window{.pos = PointI{0, top}, .size = SizeI{.w = 24, .h = height}};
        if (height == 6) {
            Canvas<SizeI{.w = 24, .h = 6}> strip;
            strip.set_clear(Color::Off);
            paint_scene(visor::ui::CanvasPainter<Canvas<SizeI{.w = 24, .h = 6}>>{strip, window});
            for (int y = 0; y < 6; ++y) {
                for (int x = 0; x < 24; ++x) {
                    static_cast<void>(replayed.set(PointI{x, top + y}, strip.get(PointI{x, y})));
                }
            }
        } else {
            Canvas<SizeI{.w = 24, .h = 4}> strip;
            strip.set_clear(Color::Off);
            paint_scene(visor::ui::CanvasPainter<Canvas<SizeI{.w = 24, .h = 4}>>{strip, window});
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 24; ++x) {
                    static_cast<void>(replayed.set(PointI{x, top + y}, strip.get(PointI{x, y})));
                }
            }
        }
    }
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 24; ++x) {
            VISOR_CHECK(whole.get(PointI{x, y}) == replayed.get(PointI{x, y}));
        }
    }
}

void draw_text_skips_invalid_bytes() {
    Canvas<SizeI{.w = 24, .h = 16}> canvas;
    canvas.set_clear(Color::Off);
    const visor::ui::CanvasPainter painter{canvas};
    draw_text(painter, PointI{0, 0}, kDemoFont,
              "\x80"
              "A",
              Color::On, 1);
    // Only A painted, at the origin: the stray byte left neither ink nor gap.
    VISOR_CHECK(canvas.get(PointI{1, 0}) == Color::On);
    VISOR_CHECK(canvas.get(PointI{0, 0}) == Color::Off);
    VISOR_CHECK(canvas.get(PointI{5, 0}) == Color::Off);
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_font_tests() {
    glyph_lookup_finds_and_misses();
    bitmap_view_reads_glyph_bits();
    measure_reports_pen_end_not_ink_extent();
    measure_counts_the_line_grid();
    measure_skips_missing_glyphs();
    measure_scales_and_clamps();
    layout_places_glyphs_on_the_line_grid();
    draw_text_paints_each_glyph_at_its_pen();
    draw_text_advances_lines_on_newline();
    draw_text_scales_blocks();
    draw_text_matches_strip_replay();
    draw_text_skips_invalid_bytes();
}
