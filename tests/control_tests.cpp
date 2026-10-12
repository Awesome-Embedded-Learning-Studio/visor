#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/label/label.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

/// A Painter double: records strokes instead of touching pixels. A sibling
/// copy of widget_tests' -- test doubles stay per-file, the same stance the
/// hand-written font assets take.
class RecordingPainter final : public visor::ui::Painter {
  public:
    struct BitmapDrawn {
        visor::base::PointI at;
        int scale = 1;
    };

    void fill([[maybe_unused]] visor::base::RectI area,
              [[maybe_unused]] visor::canvas::Color ink) const noexcept override {}
    void plot([[maybe_unused]] visor::base::PointI at,
              [[maybe_unused]] visor::canvas::Color ink) const noexcept override {}
    void draw_bitmap(visor::base::PointI at, [[maybe_unused]] const visor::draw::MonoBitmap& source,
                     [[maybe_unused]] visor::canvas::Color ink, int scale) const noexcept override {
        bitmaps_.push_back(BitmapDrawn{.at = at, .scale = scale});
    }

    [[nodiscard]] const std::vector<BitmapDrawn>& bitmaps() const noexcept { return bitmaps_; }

  private:
    mutable std::vector<BitmapDrawn> bitmaps_;
};

// A two-glyph font in the generated-header shape: 'a' is a solid 2x4 block,
// 'b' its left column, both advancing three pixels so the gap is visible.
namespace assets::label_font {
inline constexpr std::size_t kLineHeight = 4;
// NOLINTNEXTLINE(modernize-avoid-c-arrays) -- verbatim fontgen output shape.
inline constexpr std::uint8_t kBits[] = {
    0xc0, // U+0061 'a' row 0
    0xc0, // U+0061 'a' row 1
    0xc0, // U+0061 'a' row 2
    0xc0, // U+0061 'a' row 3
    0x80, // U+0062 'b' row 0
    0x80, // U+0062 'b' row 1
    0x80, // U+0062 'b' row 2
    0x80, // U+0062 'b' row 3
};
// NOLINTNEXTLINE(modernize-avoid-c-arrays) -- verbatim fontgen output shape.
inline constexpr visor::draw::GlyphEntry kGlyphs[] = {
    {.codepoint = 0x61,
     .width = 2,
     .height = 4,
     .byte_offset = 0,
     .advance = 3,
     .offset_x = 0,
     .offset_y = 0},
    {.codepoint = 0x62,
     .width = 2,
     .height = 4,
     .byte_offset = 4,
     .advance = 3,
     .offset_x = 0,
     .offset_y = 0},
};
} // namespace assets::label_font

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_control_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;

    const RectI left_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}};
    const RecordingPainter painter;

    // Label: the first control that sizes itself. Bounds come from the font's
    // measurement, set_text re-measures at the same origin, and both the old
    // and the new box are owed repaints.
    const visor::draw::MonoFont label_font{
        .line_height = assets::label_font::kLineHeight,
        .glyphs = assets::label_font::kGlyphs,
        .bits = assets::label_font::kBits,
    };
    visor::ui::Label label(PointI{2, 3}, "ab", label_font, visor::canvas::Color::On);
    VISOR_CHECK_EQ(label.bounds(), (RectI{.pos = PointI{2, 3}, .size = SizeI{.w = 6, .h = 4}}));
    VISOR_CHECK(!label.focusable());
    label.draw(painter);
    // One blit per glyph: 'a' starts at the bounds origin, 'b' at the pen
    // past 'a' (advance 3 from x=2).
    VISOR_CHECK_EQ(painter.bitmaps().size(), std::size_t{2});
    VISOR_CHECK_EQ(painter.bitmaps()[0].at, (PointI{2, 3}));
    VISOR_CHECK_EQ(painter.bitmaps()[1].at, (PointI{5, 3}));
    VISOR_CHECK_EQ(painter.bitmaps()[0].scale, 1);

    visor::ui::DirtyRegions label_reports;
    label.set_dirty_sink(&label_reports);
    label.set_text("aab");
    VISOR_CHECK_EQ(label.bounds(), (RectI{.pos = PointI{2, 3}, .size = SizeI{.w = 9, .h = 4}}));
    VISOR_CHECK_EQ(label_reports.count(), std::size_t{1}); // old and new overlap: merged
    VISOR_CHECK_EQ(label_reports.at(0),
                   (RectI{.pos = PointI{2, 3}, .size = SizeI{.w = 9, .h = 4}}));
    label_reports.clear();
    label.set_text("aab"); // same text: no pixel change, no report
    VISOR_CHECK_EQ(label_reports.count(), std::size_t{0});

    // The same story on real pixels: 'a' blocks and 'b' columns at their pens,
    // the advance gap between them stays dark.
    visor::canvas::Canvas<visor::base::SizeI{.w = 16, .h = 8}> label_frame;
    label_frame.set_clear(visor::canvas::Color::Off);
    label.draw(visor::ui::CanvasPainter{label_frame});
    VISOR_CHECK(label_frame.get(PointI{2, 3}) == visor::canvas::Color::On);  // first 'a'
    VISOR_CHECK(label_frame.get(PointI{3, 6}) == visor::canvas::Color::On);  // still the first 'a'
    VISOR_CHECK(label_frame.get(PointI{5, 3}) == visor::canvas::Color::On);  // second 'a'
    VISOR_CHECK(label_frame.get(PointI{8, 3}) == visor::canvas::Color::On);  // 'b' left column
    VISOR_CHECK(label_frame.get(PointI{9, 3}) == visor::canvas::Color::Off); // 'b' right is dark
    VISOR_CHECK(label_frame.get(PointI{4, 3}) == visor::canvas::Color::Off); // the advance gap

    // The empty text is one empty line: no width, the font's line height.
    const visor::ui::Label blank(PointI{0, 0}, "", label_font, visor::canvas::Color::On, 1);
    VISOR_CHECK_EQ(blank.bounds(), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 0, .h = 4}}));

    // Button activation, unit level: the handler is a plain function plus a
    // context word, Enter presses and its up fires, a stray up releases
    // nothing, and the pointer leg fires independently of the keyboard one.
    // Both sources share the pressed visual: whichever still holds keeps the
    // button dark after the other releases.
    int activations = 0;
    const auto count_it = [](visor::ui::Button&, void* context) { ++*static_cast<int*>(context); };
    visor::ui::Button keyed(left_bounds);
    keyed.set_on_activate(count_it, &activations);
    keyed.on_key_down(visor::host::kKeyEnter);
    VISOR_CHECK(keyed.pressed());
    keyed.on_key_up(42); // a stray up matches no held key
    VISOR_CHECK(keyed.pressed());
    VISOR_CHECK_EQ(activations, 0);
    keyed.on_key_down(visor::host::kKeySpace); // a second key while held changes nothing
    VISOR_CHECK(keyed.pressed());
    keyed.on_key_up(visor::host::kKeyEnter);
    VISOR_CHECK(!keyed.pressed());
    VISOR_CHECK_EQ(activations, 1);
    keyed.on_key_up(visor::host::kKeySpace); // the unheld Space up is nobody's
    VISOR_CHECK(!keyed.pressed());
    VISOR_CHECK_EQ(activations, 1);

    // Pointer and keyboard interleaved: each release fires its own press,
    // and the visual stays dark while either source still holds.
    visor::ui::Button both(left_bounds);
    both.set_on_activate(count_it, &activations);
    both.on_pointer_down();
    both.on_key_down(visor::host::kKeyEnter);
    VISOR_CHECK(both.pressed());
    both.on_pointer_up();
    VISOR_CHECK(both.pressed());    // the key still holds the visual
    VISOR_CHECK_EQ(activations, 2); // the pointer press fired its own release
    both.on_key_up(visor::host::kKeyEnter);
    VISOR_CHECK(!both.pressed());
    VISOR_CHECK_EQ(activations, 3);

    // A button with no handler stays quiet: the release path fires nothing.
    visor::ui::Button quiet(left_bounds);
    quiet.on_key_down(visor::host::kKeyEnter);
    quiet.on_key_up(visor::host::kKeyEnter);
    VISOR_CHECK(!quiet.pressed());

    // Checkbox has no release vocabulary to adopt: Enter's down toggles, an
    // up changes nothing (its flip is its press, the B6 reasoning stands).
    visor::ui::Checkbox upproof(left_bounds);
    upproof.on_key_down(visor::host::kKeyEnter);
    VISOR_CHECK(upproof.checked());
    upproof.on_key_up(visor::host::kKeyEnter);
    VISOR_CHECK(upproof.checked());
}
