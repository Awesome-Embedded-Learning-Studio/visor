#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/knob/knob.hpp"
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
              [[maybe_unused]] visor::canvas::ColorRole ink) const noexcept override {}
    void plot([[maybe_unused]] visor::base::PointI at,
              [[maybe_unused]] visor::canvas::ColorRole ink) const noexcept override {}
    void draw_bitmap(visor::base::PointI at, [[maybe_unused]] const visor::draw::MonoBitmap& source,
                     [[maybe_unused]] visor::canvas::ColorRole ink,
                     int scale) const noexcept override {
        bitmaps_.push_back(BitmapDrawn{.at = at, .scale = scale});
    }

    void
    fill_rounded([[maybe_unused]] visor::base::RectI area,
                 [[maybe_unused]] visor::canvas::ColorRole ink,
                 [[maybe_unused]] const visor::ui::CornerRadius& radius) const noexcept override {}

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
    {
        .codepoint = 0x61,
        .width = 2,
        .height = 4,
        .byte_offset = 0,
        .advance = 3,
        .offset_x = 0,
        .offset_y = 0,
    },
    {
        .codepoint = 0x62,
        .width = 2,
        .height = 4,
        .byte_offset = 4,
        .advance = 3,
        .offset_x = 0,
        .offset_y = 0,
    },
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
    visor::ui::Label label(PointI{2, 3}, "ab", label_font, visor::canvas::ColorRole::Foreground);
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

    // Equal measurements still owe a repaint when the glyphs change.
    label.set_text("aba");
    VISOR_CHECK_EQ(label_reports.count(), std::size_t{1});
    label.set_text("aab");
    label_reports.clear();

    // The same story on real pixels: 'a' blocks and 'b' columns at their pens,
    // the advance gap between them stays dark.
    visor::canvas::Canvas<visor::base::SizeI{.w = 16, .h = 8}> label_frame;
    label_frame.set_clear(false);
    label.draw(visor::ui::CanvasPainter{label_frame});
    VISOR_CHECK(label_frame.get(PointI{2, 3}) == true);  // first 'a'
    VISOR_CHECK(label_frame.get(PointI{3, 6}) == true);  // still the first 'a'
    VISOR_CHECK(label_frame.get(PointI{5, 3}) == true);  // second 'a'
    VISOR_CHECK(label_frame.get(PointI{8, 3}) == true);  // 'b' left column
    VISOR_CHECK(label_frame.get(PointI{9, 3}) == false); // 'b' right is dark
    VISOR_CHECK(label_frame.get(PointI{4, 3}) == false); // the advance gap

    // The empty text is one empty line: no width, the font's line height.
    const visor::ui::Label blank(PointI{0, 0}, "", label_font, visor::canvas::ColorRole::Foreground,
                                 1);
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
    keyed.on_key_down(visor::host::kKeyEnter, {});
    VISOR_CHECK(keyed.pressed());
    keyed.on_key_up(42, {}); // a stray up matches no held key
    VISOR_CHECK(keyed.pressed());
    VISOR_CHECK_EQ(activations, 0);
    keyed.on_key_down(visor::host::kKeySpace, {}); // a second key while held changes nothing
    VISOR_CHECK(keyed.pressed());
    keyed.on_key_up(visor::host::kKeyEnter, {});
    VISOR_CHECK(!keyed.pressed());
    VISOR_CHECK_EQ(activations, 1);
    keyed.on_key_up(visor::host::kKeySpace, {}); // the unheld Space up is nobody's
    VISOR_CHECK(!keyed.pressed());
    VISOR_CHECK_EQ(activations, 1);

    // Pointer and keyboard interleaved: each release fires its own press,
    // and the visual stays dark while either source still holds.
    visor::ui::Button both(left_bounds);
    both.set_on_activate(count_it, &activations);
    both.on_pointer_down(PointI{5, 5}, {});
    both.on_key_down(visor::host::kKeyEnter, {});
    VISOR_CHECK(both.pressed());
    both.on_pointer_up(PointI{5, 5});
    VISOR_CHECK(both.pressed());    // the key still holds the visual
    VISOR_CHECK_EQ(activations, 2); // the pointer press fired its own release
    both.on_key_up(visor::host::kKeyEnter, {});
    VISOR_CHECK(!both.pressed());
    VISOR_CHECK_EQ(activations, 3);

    // A button with no handler stays quiet: the release path fires nothing.
    visor::ui::Button quiet(left_bounds);
    quiet.on_key_down(visor::host::kKeyEnter, {});
    quiet.on_key_up(visor::host::kKeyEnter, {});
    VISOR_CHECK(!quiet.pressed());

    // The code-point space presses the same key: a text host types the space
    // bar as 0x20 (kCharSpace) instead of the evdev 57, and the keyboard's
    // press reads the same under both.
    visor::ui::Button typed(left_bounds);
    typed.set_on_activate(count_it, &activations);
    typed.on_key_down(visor::host::kCharSpace, {});
    VISOR_CHECK(typed.pressed());
    typed.on_key_up(visor::host::kCharSpace, {});
    VISOR_CHECK(!typed.pressed());
    VISOR_CHECK_EQ(activations, 4);

    // Checkbox has no release vocabulary to adopt: Enter's down toggles, an
    // up changes nothing (its flip is its press, the B6 reasoning stands).
    visor::ui::Checkbox upproof(left_bounds);
    upproof.on_key_down(visor::host::kKeyEnter, {});
    VISOR_CHECK(upproof.checked());
    upproof.on_key_up(visor::host::kKeyEnter, {});
    VISOR_CHECK(upproof.checked());

    // The code-point space flips too: the same duality as the button's
    // press, a text host types 0x20 for the space bar.
    visor::ui::Checkbox typed_box(left_bounds);
    typed_box.on_key_down(visor::host::kCharSpace, {});
    VISOR_CHECK(typed_box.checked());

    // The state door: set_checked writes the mark directly and owes the
    // redraw, while a write of the state it already holds changes nothing --
    // the same quiet a re-selection gets from the radio group's select.
    // Restore flows through this door, not through a synthesized key press.
    visor::ui::Checkbox stateful(left_bounds);
    visor::ui::DirtyRegions state_reports;
    stateful.set_dirty_sink(&state_reports);
    stateful.set_checked(true);
    VISOR_CHECK(stateful.checked());
    VISOR_CHECK_EQ(state_reports.count(), std::size_t{1});
    stateful.set_checked(true);
    VISOR_CHECK_EQ(state_reports.count(), std::size_t{1}); // the quiet rewrite
    state_reports.clear();
    stateful.set_checked(false);
    VISOR_CHECK(!stateful.checked());
    VISOR_CHECK_EQ(state_reports.count(), std::size_t{1}); // the flip owes its area

    // Knob: a 270-degree face the pointer aims at. The value door clamps,
    // stays quiet on a rewrite, and the keyboard steps it one at a time.
    int knob_changes = 0;
    const auto count_knob = [](visor::ui::Knob&, void* context) { ++*static_cast<int*>(context); };
    visor::ui::Knob knob(left_bounds, 0, 100, 50);
    knob.set_on_value(count_knob, &knob_changes);
    VISOR_CHECK(knob.focusable());
    VISOR_CHECK_EQ(knob.value(), 50);
    VISOR_CHECK_EQ(knob.minimum(), 0);
    VISOR_CHECK_EQ(knob.maximum(), 100);
    visor::ui::DirtyRegions knob_reports;
    knob.set_dirty_sink(&knob_reports);
    knob.set_value(120);
    VISOR_CHECK_EQ(knob.value(), 100);
    VISOR_CHECK_EQ(knob_changes, 1);
    VISOR_CHECK_EQ(knob_reports.count(), std::size_t{1});
    knob.set_value(100); // the quiet rewrite
    VISOR_CHECK_EQ(knob_changes, 1);
    knob.set_value(-5);
    VISOR_CHECK_EQ(knob.value(), 0);
    VISOR_CHECK_EQ(knob_changes, 2);
    // Keyboard: the horizontal and vertical arrows step the value one at a
    // time, in the same direction as the dial's turn.
    knob.on_key_down(visor::host::kKeyRight, {});
    VISOR_CHECK_EQ(knob.value(), 1);
    knob.on_key_down(visor::host::kKeyLeft, {});
    VISOR_CHECK_EQ(knob.value(), 0);
    // The dial follows the pointer's angle: east is 160 of the face's 192
    // steps, south lands in the gap's max half, west is 32, north is 96.
    knob.set_value(50);
    knob.on_pointer_down(PointI{9, 5}, {});
    VISOR_CHECK_EQ(knob.value(), 83);
    knob.on_pointer_move(PointI{5, 9});
    VISOR_CHECK_EQ(knob.value(), 100);
    knob.on_pointer_move(PointI{1, 5});
    VISOR_CHECK_EQ(knob.value(), 16);
    knob.on_pointer_move(PointI{5, 1});
    VISOR_CHECK_EQ(knob.value(), 50);
    knob.on_pointer_up(PointI{5, 1});
    const int knob_released = knob.value();
    knob.on_pointer_move(PointI{9, 5}); // released: moves change nothing
    VISOR_CHECK_EQ(knob.value(), knob_released);
    // A square corner is a direction, not a dead corner: the top-left corner
    // points at 225 degrees, on the low side of the face.
    knob.set_value(50);
    knob.on_pointer_down(PointI{0, 0}, {});
    VISOR_CHECK(knob.value() < 50);
    knob.on_pointer_up(PointI{0, 0});
    // The exact pivot has no angle; a press there leaves the value alone, and
    // a move out to the east turns it.
    knob.set_value(50);
    knob.on_pointer_down(PointI{5, 5}, {});
    VISOR_CHECK_EQ(knob.value(), 50);
    knob.on_pointer_move(PointI{9, 5});
    VISOR_CHECK_EQ(knob.value(), 83);
    knob.on_pointer_up(PointI{9, 5});

    // The face paints: a ring with a lit hub and enough ink for a dial, all
    // within its bounds (the theme test replays it through strips).
    const visor::ui::Knob drawable(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}}, 0,
                                   100, 50);
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> knob_frame;
    knob_frame.set_clear(false);
    drawable.draw(visor::ui::CanvasPainter{knob_frame});
    int lit = 0;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (knob_frame.get(PointI{x, y})) {
                ++lit;
            }
        }
    }
    VISOR_CHECK(lit > 40);
    VISOR_CHECK(knob_frame.get(PointI{10, 10}) == true); // the hub
}
