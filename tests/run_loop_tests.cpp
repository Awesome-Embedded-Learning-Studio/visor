#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/offscreen_host.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"

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

    // Tab arrives as a key event and moves the focus: the first press lands on
    // the first button in draw order, the second walks on, and the ring pixels
    // follow the holder.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Button one(button_bounds);
        visor::ui::Button two(RectI{.pos = PointI{10, 10}, .size = SizeI{.w = 6, .h = 4}});
        VISOR_CHECK(panel.add_child(&one));
        VISOR_CHECK(panel.add_child(&two));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 2);
        VISOR_CHECK(!one.focused());
        VISOR_CHECK(two.focused());
        // The ring is real pixels: two's border went dark inside the lit
        // panel, one's corner went back to plain lit.
        VISOR_CHECK_EQ(screen.get(PointI{10, 10}), visor::canvas::Color::Off);
        VISOR_CHECK_EQ(screen.get(PointI{2, 2}), visor::canvas::Color::On);
    }

    // Tab over a tree without a single focusable widget: consumed, counted,
    // nothing moves, nothing breaks.
    {
        visor::ui::Rectangle root(root_bounds);
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, root, frame), 1);
    }

    // A press on the checkbox flips it: the hit widget toggles on press, the
    // submitted frame carries the solid mark, and release changes nothing.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Checkbox box(RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 8, .h = 8}});
        VISOR_CHECK(panel.add_child(&box));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{6, 6}}));
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{6, 6}}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 2);
        VISOR_CHECK(box.checked());
        VISOR_CHECK_EQ(screen.get(PointI{8, 8}), visor::canvas::Color::Off);
        VISOR_CHECK_EQ(screen.get(PointI{0, 0}), visor::canvas::Color::On);
    }

    // Keyboard activation: Tab lands the focus on the checkbox, Enter flips it
    // (the frame carries the solid mark), then Space flips it back while a
    // code no control claims passes through and changes nothing.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Checkbox box(RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 8, .h = 8}});
        VISOR_CHECK(panel.add_child(&box));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyEnter}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 2);
        VISOR_CHECK(box.focused());
        VISOR_CHECK(box.checked());
        // (7,7) is the interior the mark fills; (6,6) is the frame both states share.
        VISOR_CHECK_EQ(screen.get(PointI{7, 7}), visor::canvas::Color::Off);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeySpace}));
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = 42}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 2);
        VISOR_CHECK(!box.checked());
        VISOR_CHECK_EQ(screen.get(PointI{7, 7}), visor::canvas::Color::On);
    }

    // Activation with nobody holding the focus: the key is consumed and
    // counted, reaches nobody, nothing moves.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Checkbox box(RectI{.pos = PointI{4, 4}, .size = SizeI{.w = 8, .h = 8}});
        VISOR_CHECK(panel.add_child(&box));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyEnter}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 1);
        VISOR_CHECK(!box.checked());
    }

    // The button takes no keyboard activation yet, on purpose: a momentary
    // press needs a frame showing the pressed state between down and up, so
    // Enter on a focused button is consumed and does nothing. This pins the
    // deferral until the timing story (key repeat or a tick) arrives.
    {
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Button button(button_bounds);
        VISOR_CHECK(panel.add_child(&button));
        visor::host::OffscreenHost screen(20, 20);
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyTab}));
        VISOR_CHECK(screen.push_event(visor::host::KeyDown{.code = visor::host::kKeyEnter}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, frame), 2);
        VISOR_CHECK(button.focused());
        VISOR_CHECK(!button.pressed());
    }
}
