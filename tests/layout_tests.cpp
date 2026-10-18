#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include <cstddef>

#include "visor/bindings/events.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/box/box.hpp"
#include "visor/ui/widgets/slider/slider.hpp"

void run_layout_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;

    const RectI row_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 6}};

    // HBox keeps each child's width, stretches every child to the row's
    // height, and stacks left to right with the spacing between.
    {
        visor::ui::HBox row(row_bounds);
        visor::ui::Rectangle slim(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 1}});
        visor::ui::Rectangle wide(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 2}});
        visor::ui::Rectangle sliver(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 2, .h = 3}});
        VISOR_CHECK(row.add_child(&slim));
        VISOR_CHECK(row.add_child(&wide));
        VISOR_CHECK(row.add_child(&sliver));
        row.layout();
        VISOR_CHECK_EQ(slim.bounds(), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 6}}));
        VISOR_CHECK_EQ(wide.bounds(), (RectI{.pos = PointI{4, 0}, .size = SizeI{.w = 6, .h = 6}}));
        VISOR_CHECK_EQ(sliver.bounds(),
                       (RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 2, .h = 6}}));

        // Spacing steps the cursor; a relayout after the change re-arranges.
        row.set_spacing(2);
        row.layout();
        VISOR_CHECK_EQ(wide.bounds(), (RectI{.pos = PointI{6, 0}, .size = SizeI{.w = 6, .h = 6}}));
        VISOR_CHECK_EQ(sliver.bounds(),
                       (RectI{.pos = PointI{14, 0}, .size = SizeI{.w = 2, .h = 6}}));
    }

    // VBox is the same walk with the axes swapped: heights kept, widths
    // stretched, stacked top to bottom, spacing between.
    {
        visor::ui::VBox column(row_bounds);
        visor::ui::Rectangle top(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 1, .h = 2}});
        visor::ui::Rectangle bottom(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 9, .h = 3}});
        VISOR_CHECK(column.add_child(&top));
        VISOR_CHECK(column.add_child(&bottom));
        column.set_spacing(1);
        column.layout();
        VISOR_CHECK_EQ(top.bounds(), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 2}}));
        VISOR_CHECK_EQ(bottom.bounds(),
                       (RectI{.pos = PointI{0, 3}, .size = SizeI{.w = 20, .h = 3}}));
    }

    // A layout move is a set_bounds move: the old and new boxes are owed
    // repaint, and a layout that changes nothing owes nothing new.
    {
        visor::ui::HBox row(row_bounds);
        visor::ui::Rectangle first(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 6}});
        visor::ui::Rectangle second(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 6}});
        VISOR_CHECK(row.add_child(&first));
        VISOR_CHECK(row.add_child(&second));
        visor::ui::DirtyRegions debts;
        row.set_dirty_sink(&debts);
        row.layout();
        // first keeps its box (already at 0,0,4,6); second moves to x=4. The
        // old and new box share an edge, and half-open adjacency is no
        // overlap -- the collector keeps them as two regions, chapter two's
        // teststone applying at home.
        VISOR_CHECK_EQ(debts.count(), std::size_t{2});
        VISOR_CHECK_EQ(debts.at(0), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 6}}));
        VISOR_CHECK_EQ(debts.at(1), (RectI{.pos = PointI{4, 0}, .size = SizeI{.w = 4, .h = 6}}));
        debts.clear();
        row.layout(); // nothing moves the second time
        VISOR_CHECK_EQ(debts.count(), std::size_t{0});
    }

    // Nested boxes compose caller-side: the outer pass sets the inner box's
    // bounds, then the inner pass arranges its own children inside them.
    {
        visor::ui::HBox outer(row_bounds);
        visor::ui::VBox inner(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 6}});
        visor::ui::Rectangle tail(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 6}});
        visor::ui::Rectangle cell_a(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 2}});
        visor::ui::Rectangle cell_b(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 3}});
        VISOR_CHECK(outer.add_child(&inner));
        VISOR_CHECK(outer.add_child(&tail));
        VISOR_CHECK(inner.add_child(&cell_a));
        VISOR_CHECK(inner.add_child(&cell_b));
        outer.layout();
        inner.layout(); // after the outer pass set the inner box's bounds
        VISOR_CHECK_EQ(inner.bounds(), (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 6}}));
        VISOR_CHECK_EQ(cell_a.bounds(),
                       (RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 6, .h = 2}}));
        VISOR_CHECK_EQ(cell_b.bounds(),
                       (RectI{.pos = PointI{0, 2}, .size = SizeI{.w = 6, .h = 3}}));
        VISOR_CHECK_EQ(tail.bounds(), (RectI{.pos = PointI{6, 0}, .size = SizeI{.w = 10, .h = 6}}));
    }

    // The slider, end to end through the router: the press grabs (the value
    // jumps to it), the moves drag -- capture keeps them coming outside the
    // bounds too, clamping at both ends -- and a hover that never pressed
    // changes nothing.
    {
        int changes = 0;
        const auto count_it = [](visor::ui::Slider&, void* context) {
            ++*static_cast<int*>(context);
        };
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 24, .h = 8}});
        visor::ui::Slider knob(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 16, .h = 4}});
        knob.set_on_value(count_it, &changes);
        VISOR_CHECK(panel.add_child(&knob));
        visor::host::OffscreenHost screen(24, 8);
        visor::ui::InputRouter input;
        const auto host = screen.table();
        Canvas<SizeI{.w = 24, .h = 8}> frame;

        VISOR_CHECK_EQ(knob.max(), 9); // 16 of track minus the 7-wide handle
        // A hover that never pressed is nobody's value change.
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{10, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(knob.value(), 0);
        VISOR_CHECK_EQ(changes, 0);
        // The press grabs at x=6: relative 4.
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{6, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(knob.value(), 4);
        VISOR_CHECK_EQ(changes, 1);
        // The drag walks past the track's right end: clamped at max.
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{40, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(knob.value(), 9);
        // Back in and past the left end: clamped at 0.
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{1, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(knob.value(), 0);
        VISOR_CHECK_EQ(changes, 3); // 4 -> 9 -> 0, one change per landing
        // The release lets go; later moves hover past unheard.
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{1, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{12, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(knob.value(), 0);
        VISOR_CHECK_EQ(changes, 3);
    }

    // A hollow thumb stays distinct from the thin track on both polarities.
    {
        visor::ui::Rectangle panel(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 32, .h = 16}});
        panel.fill = visor::canvas::ColorRole::Background;
        visor::ui::Slider knob(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 24, .h = 12}});
        VISOR_CHECK(panel.add_child(&knob));
        knob.set_value(6);
        visor::canvas::Canvas<SizeI{.w = 32, .h = 16}> canvas;
        panel.draw(visor::ui::CanvasPainter{canvas});
        const auto on = true;
        const auto off = false;
        VISOR_CHECK(canvas.get(PointI{2, 8}) == on);   // track start
        VISOR_CHECK(canvas.get(PointI{25, 8}) == on);  // track end
        VISOR_CHECK(canvas.get(PointI{8, 7}) == on);   // thumb outline
        VISOR_CHECK(canvas.get(PointI{11, 7}) == off); // hollow thumb
        VISOR_CHECK(canvas.get(PointI{7, 8}) == on);   // adjoining track
        VISOR_CHECK(canvas.get(PointI{15, 8}) == on);
        VISOR_CHECK(canvas.get(PointI{8, 2}) == off); // outside the thumb
        knob.set_focused(true);
        knob.on_key_down(visor::host::kKeyRight, {});
        VISOR_CHECK_EQ(knob.value(), 7);
        knob.on_key_down(visor::host::kKeyLeft, {});
        VISOR_CHECK_EQ(knob.value(), 6);
        visor::ui::Slider tiny(RectI{.pos = {0, 0}, .size = {.w = 2, .h = 2}});
        VISOR_CHECK_EQ(tiny.max(), 0);
        tiny.set_value(20);
        VISOR_CHECK_EQ(tiny.value(), 0);
    }
}
