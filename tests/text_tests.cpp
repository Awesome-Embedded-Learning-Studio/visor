#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/textbox/textbox.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;

// A three-glyph font in the generated-header shape: h, i, o, each 2 wide and
// 4 tall advancing 3 -- enough alphabet for an editor's first words.
namespace assets::type_font {
inline constexpr std::size_t kLineHeight = 4;
// NOLINTNEXTLINE(modernize-avoid-c-arrays) -- verbatim fontgen output shape.
inline constexpr std::uint8_t kBits[] = {
    0xc0, // U+0068 'h' row 0
    0xc0, // U+0068 'h' row 1
    0xc0, // U+0068 'h' row 2
    0xc0, // U+0068 'h' row 3
    0xc0, // U+0069 'i' row 0
    0x80, // U+0069 'i' row 1
    0x80, // U+0069 'i' row 2
    0xc0, // U+0069 'i' row 3
    0xc0, // U+006F 'o' row 0
    0xc0, // U+006F 'o' row 1
    0xc0, // U+006F 'o' row 2
    0xc0, // U+006F 'o' row 3
};
// NOLINTNEXTLINE(modernize-avoid-c-arrays) -- verbatim fontgen output shape.
inline constexpr visor::draw::GlyphEntry kGlyphs[] = {
    {
        .codepoint = 0x68,
        .width = 2,
        .height = 4,
        .byte_offset = 0,
        .advance = 3,
        .offset_x = 0,
        .offset_y = 0,
    },
    {
        .codepoint = 0x69,
        .width = 2,
        .height = 4,
        .byte_offset = 4,
        .advance = 3,
        .offset_x = 0,
        .offset_y = 0,
    },
    {
        .codepoint = 0x6f,
        .width = 2,
        .height = 4,
        .byte_offset = 8,
        .advance = 3,
        .offset_x = 0,
        .offset_y = 0,
    },
};
} // namespace assets::type_font

constexpr visor::draw::MonoFont kTypeFont{
    .line_height = assets::type_font::kLineHeight,
    .glyphs = assets::type_font::kGlyphs,
    .bits = assets::type_font::kBits,
};

} // namespace

void run_text_tests() {
    const RectI box_bounds{.pos = PointI{2, 2}, .size = SizeI{.w = 22, .h = 12}};

    // Typing accumulates at the caret, through the whole stack: Tab focuses
    // the editor, printable codes land as characters, Backspace deletes
    // before the caret, and Enter submits without inserting.
    {
        int changes = 0;
        int submits = 0;
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 24, .h = 8}});
        visor::ui::TextBox box(box_bounds, kTypeFont, visor::canvas::Color::On);
        box.set_on_change([](visor::ui::TextBox&, void* context) { ++*static_cast<int*>(context); },
                          &changes);
        box.set_on_submit([](visor::ui::TextBox&, void* context) { ++*static_cast<int*>(context); },
                          &submits);
        VISOR_CHECK(panel.add_child(&box));
        visor::host::OffscreenHost screen(24, 8);
        visor::ui::InputRouter input;
        const auto host = screen.table();
        Canvas<SizeI{.w = 32, .h = 20}> frame;

        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(box.focused());
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = 'h'}));
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = 'i'}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1); // one key per round
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(box.text(), "hi");
        VISOR_CHECK_EQ(changes, 2);
        // Backspace deletes before the caret.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyBackspace}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(box.text(), "h");
        VISOR_CHECK_EQ(changes, 3);
        // Left moves the caret back, typing inserts in the middle.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyLeft}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = 'o'}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(box.text(), "oh");
        VISOR_CHECK_EQ(changes, 4);
        // Right walks forward, clamped at the end.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyRight}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyRight}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        // Enter submits, inserts nothing, fires nothing else.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyEnter}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(box.text(), "oh");
        VISOR_CHECK_EQ(changes, 4);
        VISOR_CHECK_EQ(submits, 1);
    }

    // The honest refusals: control codes below 0x20 (Enter and friends
    // already named) and 0x7F are not text, Up and Down are a single line's
    // non-events, and a full buffer drops further typing.
    {
        visor::ui::TextBox box(box_bounds, kTypeFont, visor::canvas::Color::On);
        box.on_key_down(0x1f); // below the printable range
        box.on_key_down(0x7f); // DEL, past it
        box.on_key_down(visor::host::kKeyUp);
        box.on_key_down(visor::host::kKeyDown);
        VISOR_CHECK_EQ(box.text(), "");
        // Backspace and Left on an empty box change nothing.
        box.on_key_down(visor::host::kKeyBackspace);
        box.on_key_down(visor::host::kKeyLeft);
        VISOR_CHECK_EQ(box.text(), "");
    }

    // Capacity is a compile-time fact: a set_text longer than the buffer
    // truncates, typing past a full buffer drops.
    {
        visor::ui::TextBox box(box_bounds, kTypeFont, visor::canvas::Color::On);
        const std::string sixty(visor::ui::kTextBoxCapacity + 4, 'h');
        box.set_text(sixty);
        VISOR_CHECK_EQ(box.text().size(), visor::ui::kTextBoxCapacity);
        box.on_key_down('o'); // full: dropped
        VISOR_CHECK_EQ(box.text().size(), visor::ui::kTextBoxCapacity);
        // Backspace frees one slot; the next key lands.
        box.on_key_down(visor::host::kKeyBackspace);
        box.on_key_down('o');
        VISOR_CHECK_EQ(box.text().back(), 'o');
    }

    // set_text states the whole text: same words report nothing, different
    // words fire the change.
    {
        int changes = 0;
        visor::ui::TextBox box(box_bounds, kTypeFont, visor::canvas::Color::On);
        box.set_on_change([](visor::ui::TextBox&, void* context) { ++*static_cast<int*>(context); },
                          &changes);
        box.set_text("hi");
        VISOR_CHECK_EQ(changes, 1);
        box.set_text("hi");
        VISOR_CHECK_EQ(changes, 1);
        box.set_text("ho");
        VISOR_CHECK_EQ(changes, 2);
        VISOR_CHECK_EQ(box.text(), "ho");
        // The caret sits at the end: backspace takes the o.
        box.on_key_down(visor::host::kKeyBackspace);
        VISOR_CHECK_EQ(box.text(), "h");
    }

    // Clicking an editor takes keyboard focus from the previous control.
    // Subsequent characters must change this editor, and Tab continues from it.
    {
        visor::ui::Rectangle panel(RectI{.pos = {0, 0}, .size = {.w = 64, .h = 24}});
        visor::ui::TextBox first(RectI{.pos = {2, 2}, .size = {.w = 22, .h = 12}}, kTypeFont,
                                 visor::canvas::Color::On);
        visor::ui::TextBox second(RectI{.pos = {28, 2}, .size = {.w = 22, .h = 12}}, kTypeFont,
                                  visor::canvas::Color::On);
        VISOR_CHECK(panel.add_child(&first));
        VISOR_CHECK(panel.add_child(&second));
        visor::host::OffscreenHost screen(64, 24);
        visor::ui::InputRouter input;
        Canvas<SizeI{.w = 64, .h = 24}> frame;
        VISOR_CHECK(panel.advance_focus());
        VISOR_CHECK(first.focused());
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = {32, 6}}));
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = {32, 6}}));
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = 'h'}));
        static_cast<void>(visor::ui::run_once(screen.table(), panel, input, frame));
        VISOR_CHECK(!first.focused());
        VISOR_CHECK(second.focused());
        VISOR_CHECK_EQ(first.text(), "");
        VISOR_CHECK_EQ(second.text(), "h");
        VISOR_CHECK(panel.advance_focus());
        VISOR_CHECK(first.focused());
        VISOR_CHECK(!second.focused());
    }

    // Text and the focused caret sit inside a padded outline. Moving the
    // caret leaves the field border intact.
    {
        visor::ui::TextBox box(box_bounds, kTypeFont, visor::canvas::Color::On);
        box.set_text("h");
        Canvas<SizeI{.w = 32, .h = 20}> canvas;
        const visor::ui::CanvasPainter painter{canvas};
        const auto on = visor::canvas::Color::On;
        const auto off = visor::canvas::Color::Off;
        box.draw(painter);
        VISOR_CHECK(canvas.get(PointI{2, 6}) == on); // border
        VISOR_CHECK(canvas.get(PointI{5, 6}) == on); // glyph
        VISOR_CHECK(canvas.get(PointI{6, 9}) == on);
        VISOR_CHECK(canvas.get(PointI{7, 6}) == off); // advance gap
        VISOR_CHECK(canvas.get(PointI{8, 6}) == off); // unfocused caret absent
        box.set_focused(true);
        canvas.set_clear(off);
        box.draw(painter);
        VISOR_CHECK(canvas.get(PointI{8, 6}) == on);
        VISOR_CHECK(canvas.get(PointI{8, 9}) == on);
        VISOR_CHECK(canvas.get(PointI{9, 6}) == off);
        box.on_key_down(visor::host::kKeyLeft);
        canvas.set_clear(off);
        box.draw(painter);
        VISOR_CHECK(canvas.get(PointI{5, 6}) == on);  // caret before glyph
        VISOR_CHECK(canvas.get(PointI{6, 6}) == on);  // glyph unchanged
        VISOR_CHECK(canvas.get(PointI{8, 6}) == off); // previous caret erased
    }

    // The window slides: a box with 16 pixels of content shows at most the run that keeps the
    // caret inside, and deletion pulls the window back.
    {
        visor::ui::TextBox box(box_bounds, kTypeFont, visor::canvas::Color::On);
        // 8 glyphs of advance 3 = 24px past its content area: the window must slide
        // until the caret fits with its column.
        box.set_text("iiiihhhh");
        VISOR_CHECK_EQ(box.text().size(), std::size_t{8});
        Canvas<SizeI{.w = 32, .h = 20}> canvas;
        box.set_focused(true);
        canvas.set_clear(visor::canvas::Color::Off);
        box.draw(visor::ui::CanvasPainter{canvas});
        // The window slid to index 3 (five glyphs of advance 3 fit 15px of
        // room): the first visible glyph is 'i', whose interior-right column
        // is dark on the middle rows -- a solid 'h' there would prove
        // nothing. The second glyph's ink proves text continues after it.
        VISOR_CHECK(canvas.get(PointI{6, 7}) == visor::canvas::Color::Off);
        VISOR_CHECK(canvas.get(PointI{8, 7}) == visor::canvas::Color::On);
        VISOR_CHECK(canvas.get(PointI{20, 7}) == visor::canvas::Color::On);  // caret at tail
        VISOR_CHECK(canvas.get(PointI{21, 7}) == visor::canvas::Color::Off); // right padding
        VISOR_CHECK(canvas.get(PointI{24, 6}) == visor::canvas::Color::Off); // outside the box
        // Deleting down to one glyph pulls the caret left of the window, and
        // the window follows: the surviving 'i' shows at the box's own origin
        // again (left column ink, right column dark on middle rows), the
        // caret after it at x = 8.
        for (int i = 0; i < 7; ++i) {
            box.on_key_down(visor::host::kKeyBackspace);
        }
        VISOR_CHECK_EQ(box.text(), "i");
        canvas.set_clear(visor::canvas::Color::Off);
        box.draw(visor::ui::CanvasPainter{canvas});
        VISOR_CHECK(canvas.get(PointI{6, 7}) == visor::canvas::Color::Off); // 'i', not 'h'
        VISOR_CHECK(canvas.get(PointI{8, 7}) == visor::canvas::Color::On);  // the caret
    }
}
