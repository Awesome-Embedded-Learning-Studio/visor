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
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/popup_layer.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/dropdown/dropdown.hpp"
#include "visor/ui/widgets/radio/radio.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::canvas::Canvas;
using visor::canvas::ColorRole;

// The same three-glyph font the text judgments ride (h, i, o, advance 3,
// line height 4): enough alphabet for a choice list's first words.
namespace assets::choice_font {
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
} // namespace assets::choice_font

constexpr visor::draw::MonoFont kChoiceFont{
    .line_height = assets::choice_font::kLineHeight,
    .glyphs = assets::choice_font::kGlyphs,
    .bits = assets::choice_font::kBits,
};

} // namespace

void run_choice_tests() {
    // The radio group: options stack with six pixels of vertical padding, a press on a
    // row selects it, the arrows walk with clamping, and selection changes
    // fire exactly once per change.
    {
        int changes = 0;
        visor::ui::RadioGroup group(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 20, .h = 30}},
                                    kChoiceFont, visor::canvas::ColorRole::Foreground);
        group.set_on_change(
            [](visor::ui::RadioGroup&, void* context) { ++*static_cast<int*>(context); }, &changes);
        VISOR_CHECK(group.add_option("hi"));
        VISOR_CHECK(group.add_option("ho"));
        VISOR_CHECK(group.add_option("oh"));
        VISOR_CHECK_EQ(group.selected(), -1);         // nobody touched it yet
        group.on_pointer_down(PointI{3, 2 + 10}, {}); // the second row (y 12..21)
        VISOR_CHECK_EQ(group.selected(), 1);
        VISOR_CHECK_EQ(changes, 1);
        group.on_pointer_down(PointI{3, 2 + 10}, {}); // the same row again
        VISOR_CHECK_EQ(changes, 1);                   // a re-selection is no change
        // Down walks, clamped at the bottom.
        group.on_key_down(visor::host::kKeyDown, {});
        VISOR_CHECK_EQ(group.selected(), 2);
        group.on_key_down(visor::host::kKeyDown, {});
        VISOR_CHECK_EQ(group.selected(), 2);
        VISOR_CHECK_EQ(changes, 2);
        // Up walks, clamped at the top; a press between rows picks nothing.
        group.on_key_down(visor::host::kKeyUp, {});
        group.on_key_down(visor::host::kKeyUp, {});
        group.on_key_down(visor::host::kKeyUp, {});
        VISOR_CHECK_EQ(group.selected(), 0);
        VISOR_CHECK_EQ(changes, 4); // 2 -> 1 -> 0 is two moves, not three
        group.select(7);            // out of range asks nothing
        VISOR_CHECK_EQ(group.selected(), 0);
        VISOR_CHECK_EQ(changes, 4);
        // Enter and Space are nothing here: a radio has no moment to
        // complete, its state moves as it moves.
        group.on_key_down(visor::host::kKeyEnter, {});
        group.on_key_down(visor::host::kKeySpace, {});
        VISOR_CHECK_EQ(group.selected(), 0);
        VISOR_CHECK_EQ(changes, 4);
    }

    // Circular radio outlines have an empty center until selected, with a
    // gap between the selected dot and its ring.
    {
        visor::ui::RadioGroup group(RectI{.pos = {2, 2}, .size = {.w = 28, .h = 20}}, kChoiceFont,
                                    visor::canvas::ColorRole::Foreground);
        VISOR_CHECK(group.add_option("hi"));
        VISOR_CHECK(group.add_option("ho"));
        group.select(1);
        Canvas<SizeI{.w = 32, .h = 24}> canvas;
        group.draw(visor::ui::CanvasPainter{canvas});
        VISOR_CHECK(canvas.get(PointI{7, 3}) == true);    // ring top
        VISOR_CHECK(canvas.get(PointI{7, 6}) == false);   // first option empty
        VISOR_CHECK(canvas.get(PointI{7, 16}) == true);   // selected dot
        VISOR_CHECK(canvas.get(PointI{7, 14}) == false);  // gap around dot
        VISOR_CHECK(canvas.get(PointI{15, 15}) == true);  // second label
        VISOR_CHECK(canvas.get(PointI{14, 15}) == false); // gap before label
    }

    // The dropdown: closed it is one row, a press opens it into the list
    // (bounds grow by set_bounds, both boxes owed), a press on an open row
    // picks and closes, Enter picks the highlighted row.
    {
        int changes = 0;
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 32, .h = 16}});
        visor::ui::Dropdown list(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 12, .h = 4}},
                                 kChoiceFont, visor::canvas::ColorRole::Foreground);
        list.set_on_change(
            [](visor::ui::Dropdown&, void* context) { ++*static_cast<int*>(context); }, &changes);
        VISOR_CHECK(list.add_option("hi"));
        VISOR_CHECK(list.add_option("ho"));
        VISOR_CHECK(list.add_option("oh"));
        VISOR_CHECK(panel.add_child(&list));
        VISOR_CHECK_EQ(list.selected(), -1);
        VISOR_CHECK(!list.open());

        visor::host::OffscreenHost screen(32, 16);
        visor::ui::InputRouter input;
        const auto host = screen.table();
        Canvas<SizeI{.w = 32, .h = 16}> frame;
        // A press opens: three rows now, bounds grown from 4 to 12 tall.
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(list.open());
        VISOR_CHECK_EQ(list.bounds(),
                       (RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 12, .h = 12}}));
        // Nothing selected yet; the highlight starts at the first option.
        VISOR_CHECK_EQ(list.selected(), -1);
        // The keyboard leg needs the focus: the dropdown is the panel's only
        // focusable child, one Tab lands on it.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(list.focused());
        // Down walks the highlight (clamped), Enter picks and closes.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyDown}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyDown}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyDown}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1); // clamped at 2
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyEnter}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(!list.open());
        VISOR_CHECK_EQ(list.selected(), 2);
        VISOR_CHECK_EQ(list.bounds(), (RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 12, .h = 4}}));
        VISOR_CHECK_EQ(changes, 1);
        // The closed row now shows the picked option's label; the keyboard
        // opens too (Space), and a press on an open row picks directly.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeySpace}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(list.open());
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 3}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(!list.open());
        VISOR_CHECK_EQ(list.selected(), 0);
        VISOR_CHECK_EQ(changes, 2);
        // The code-point space opens too: a text host types 0x20 for the
        // space bar, the same press under either number.
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kCharSpace}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(list.open());
    }

    // A closed dropdown has an outline and a centered chevron; opening it
    // inverts only the selected row, preserving the panel border.
    {
        visor::ui::Dropdown list(RectI{.pos = {2, 2}, .size = {.w = 28, .h = 12}}, kChoiceFont,
                                 visor::canvas::ColorRole::Foreground);
        VISOR_CHECK(list.add_option("hi"));
        VISOR_CHECK(list.add_option("ho"));
        list.select(1);
        Canvas<SizeI{.w = 32, .h = 30}> canvas;
        list.draw(visor::ui::CanvasPainter{canvas});
        VISOR_CHECK(canvas.get(PointI{6, 6}) == true);   // label
        VISOR_CHECK(canvas.get(PointI{15, 6}) == false); // empty field
        VISOR_CHECK(canvas.get(PointI{2, 6}) == true);   // outline
        VISOR_CHECK(canvas.get(PointI{23, 8}) == true);  // arrow tip
        list.on_key_down(visor::host::kKeyEnter, {});
        canvas.set_clear(false);
        list.draw(visor::ui::CanvasPainter{canvas});
        VISOR_CHECK(list.open());
        VISOR_CHECK(canvas.get(PointI{6, 18}) == false); // selected label reversed
        VISOR_CHECK(canvas.get(PointI{12, 18}) == true); // selected row
        VISOR_CHECK(canvas.get(PointI{6, 6}) == true);   // first label
        VISOR_CHECK(canvas.get(PointI{12, 6}) == false); // first row background
    }

    // The dropdown's open-close cycle reports both boxes: the growth and the
    // collapse each owe their old and new area.
    {
        visor::ui::Dropdown list(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 12, .h = 4}},
                                 kChoiceFont, visor::canvas::ColorRole::Foreground);
        VISOR_CHECK(list.add_option("hi"));
        VISOR_CHECK(list.add_option("ho"));
        visor::ui::DirtyRegions debts;
        list.set_dirty_sink(&debts);
        list.set_open(true);
        // The grown box nests the closed one, so the two reports merge into
        // a single region covering the whole open list.
        VISOR_CHECK_EQ(debts.count(), std::size_t{1});
        VISOR_CHECK_EQ(debts.at(0), (RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 12, .h = 8}}));
        debts.clear();
        list.set_open(false);
        VISOR_CHECK_EQ(debts.count(), std::size_t{1}); // the collapse nests the same way
        VISOR_CHECK_EQ(debts.at(0), (RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 12, .h = 8}}));
        debts.clear();
        list.set_open(false); // already closed, nothing owed
        VISOR_CHECK_EQ(debts.count(), std::size_t{0});
    }

    // With a popup layer, the open list is a member mounted on the layer:
    // the dropdown's own bounds stay closed, the list hangs past its window,
    // an outside press dismisses, and a row press or Enter picks.
    {
        int changes = 0;
        visor::ui::Rectangle root(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 64, .h = 64}});
        visor::ui::Rectangle window(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 30, .h = 14}});
        visor::ui::Dropdown list(RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 20, .h = 6}},
                                 kChoiceFont, visor::canvas::ColorRole::Foreground);
        visor::ui::PopupLayer layer(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 64, .h = 64}});
        root.fill = ColorRole::Background;
        window.fill = ColorRole::Background;
        VISOR_CHECK(root.add_child(&window));
        VISOR_CHECK(window.add_child(&list));
        VISOR_CHECK(root.add_child(&layer));
        list.set_popup(&layer);
        VISOR_CHECK(list.add_option("hi"));
        VISOR_CHECK(list.add_option("ho"));
        VISOR_CHECK(list.add_option("oh"));
        list.set_on_change(
            [](visor::ui::Dropdown&, void* context) { ++*static_cast<int*>(context); }, &changes);
        const RectI closed{.pos = PointI{4, 4}, .size = SizeI{.w = 20, .h = 6}};

        list.set_open(true);
        VISOR_CHECK(list.open());
        VISOR_CHECK(layer.is_open());
        VISOR_CHECK_EQ(list.bounds(), closed); // no in-tree growth
        VISOR_CHECK(layer.content() != nullptr);
        if (layer.content() != nullptr) {
            VISOR_CHECK_EQ(layer.content()->bounds(),
                           (RectI{.pos = PointI{4, 10}, .size = SizeI{.w = 20, .h = 18}}));
        }
        VISOR_CHECK_EQ(root.hit_test(PointI{10, 12}), layer.content());

        Canvas<SizeI{.w = 64, .h = 64}> canvas;
        root.draw(visor::ui::CanvasPainter{canvas});
        VISOR_CHECK(canvas.get(PointI{20, 12}) == true);  // first row highlighted
        VISOR_CHECK(canvas.get(PointI{20, 18}) == false); // second row plain

        visor::ui::InputRouter input;
        visor::ui::dispatch_event(
            root, input,
            visor::host::Event{visor::host::PointerDown{.button = 1, .pos = PointI{50, 50}}}, 0,
            true);
        visor::ui::dispatch_event(
            root, input,
            visor::host::Event{visor::host::PointerUp{.button = 1, .pos = PointI{50, 50}}}, 0,
            true);
        VISOR_CHECK(!list.open());
        VISOR_CHECK(!layer.is_open());
        VISOR_CHECK_EQ(list.bounds(), closed);

        list.set_open(true);
        list.on_key_down(visor::host::kKeyDown, {});
        list.on_key_down(visor::host::kKeyEnter, {});
        VISOR_CHECK(!list.open());
        VISOR_CHECK_EQ(list.selected(), 1);
        VISOR_CHECK_EQ(changes, 1);

        list.set_open(true);
        visor::ui::dispatch_event(
            root, input,
            visor::host::Event{visor::host::PointerDown{.button = 1, .pos = PointI{10, 25}}}, 10,
            true);
        visor::ui::dispatch_event(
            root, input,
            visor::host::Event{visor::host::PointerUp{.button = 1, .pos = PointI{10, 25}}}, 10,
            true);
        VISOR_CHECK(!list.open());
        VISOR_CHECK_EQ(list.selected(), 2);
        VISOR_CHECK_EQ(changes, 2);
        VISOR_CHECK_EQ(list.bounds(), closed);
    }
}
