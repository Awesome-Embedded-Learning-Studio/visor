#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/button/button.hpp"

void run_run_loop_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;

    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}};
    const RectI button_bounds{.pos = PointI{2, 2}, .size = SizeI{.w = 6, .h = 4}};

    // Key and movement events over a tree without any handler: they are
    // consumed, the frame still goes out, nothing breaks.
    {
        visor::ui::Rectangle root(root_bounds);
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = 42}));
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{1, 1}}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, root, frame), 2);
        VISOR_CHECK_EQ(screen.flush_count(), 1);
    }

    // A press on the button: the hit widget consumes it, the state flips, and
    // the frame submitted after the dispatch carries the new pixels.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Button button(button_bounds);
        VISOR_CHECK(panel.add_child(&button));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 4}}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 1);
        VISOR_CHECK(button.pressed());
        // Pressed paints Off on top of the panel's On: the flip is visible.
        VISOR_CHECK_EQ(screen.get(PointI{4, 4}), visor::canvas::Color::Off);
        VISOR_CHECK_EQ(screen.get(PointI{15, 15}), visor::canvas::Color::On);
    }

    // A release landing outside the button reaches some other widget, so the
    // button never hears about it and stays pressed until told otherwise.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Button button(button_bounds);
        VISOR_CHECK(panel.add_child(&button));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 4}}));
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{15, 15}}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 2);
        VISOR_CHECK(button.pressed());
        VISOR_CHECK_EQ(screen.get(PointI{4, 4}), visor::canvas::Color::Off);
    }
}
