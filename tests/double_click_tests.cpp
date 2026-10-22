#include "cases.hpp"
#include "testing.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <cstdint>
#include <limits>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::Modifiers;
using visor::ui::InputRouter;

class ClickSpy final : public visor::ui::Widget {
  public:
    using Widget::Widget;

    void on_pointer_up(PointI at) noexcept override {
        ++ups;
        last_up = at;
    }

    void on_double_click(PointI at, Modifiers modifiers) noexcept override {
        ++doubles;
        last_double = at;
        last_modifiers = modifiers;
    }

    int ups = 0;
    int doubles = 0;
    PointI last_up;
    PointI last_double;
    Modifiers last_modifiers;

  protected:
    void on_draw([[maybe_unused]] const visor::ui::Painter& painter) const noexcept override {}
};

void click(visor::ui::Widget& root, InputRouter& input, PointI at, std::uint64_t now,
           bool clock_available = true, std::uint32_t button = 1,
           Modifiers modifiers = {}) noexcept {
    visor::ui::dispatch_event(
        root, input,
        Event{visor::host::PointerDown{.button = button, .pos = at, .modifiers = modifiers}}, now,
        clock_available);
    visor::ui::dispatch_event(root, input,
                              Event{visor::host::PointerUp{.button = button, .pos = at}}, now,
                              clock_available);
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_double_click_tests() {
    // The ordinary pair: two primary clicks on one widget, inside the window
    // and the slop, call on_double_click once after the second release. Every
    // click still runs on_pointer_up immediately, so the single-click path is
    // never delayed.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 10, .h = 6}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;

        click(desktop, input, PointI{5, 5}, 100);
        VISOR_CHECK_EQ(spy.ups, 1);
        VISOR_CHECK_EQ(spy.doubles, 0);

        click(desktop, input, PointI{6, 6}, 300, true, 1, Modifiers{.ctrl = true});
        VISOR_CHECK_EQ(spy.ups, 2);
        VISOR_CHECK_EQ(spy.doubles, 1);
        VISOR_CHECK_EQ(spy.last_double, (PointI{6, 6}));
        VISOR_CHECK(spy.last_modifiers.ctrl);
        VISOR_CHECK(!spy.last_modifiers.shift);
    }

    // The window includes its boundary: exactly 500 ms counts, 501 does not.
    // A pair that misses becomes the first click of a new pair, so the next
    // click can still pair with it.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{5, 5}, 600);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{5, 5}, 601);
        VISOR_CHECK_EQ(spy.doubles, 0);
        click(desktop, input, PointI{5, 5}, 1050);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }

    // A click on another widget is not a pair. It replaces the pending click
    // instead, so the second widget can start its own pair.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 40, .h = 20}});
        ClickSpy first(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        ClickSpy second(RectI{.pos = PointI{20, 0}, .size = SizeI{.w = 10, .h = 10}});
        VISOR_CHECK(desktop.add_child(&first));
        VISOR_CHECK(desktop.add_child(&second));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{25, 5}, 200);
        VISOR_CHECK_EQ(first.doubles, 0);
        VISOR_CHECK_EQ(second.doubles, 0);
        click(desktop, input, PointI{26, 5}, 300);
        VISOR_CHECK_EQ(second.doubles, 1);
    }

    // The slop is per axis and inclusive: four pixels still pairs, five does
    // not.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{9, 9}, 200);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{10, 5}, 200);
        VISOR_CHECK_EQ(spy.doubles, 0);
    }

    // Only the primary button pairs. A right click never starts a pair, and a
    // right click between two left clicks breaks the pending pair.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100, true, 3);
        click(desktop, input, PointI{5, 5}, 200, true, 3);
        VISOR_CHECK_EQ(spy.ups, 2);
        VISOR_CHECK_EQ(spy.doubles, 0);
    }
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{5, 5}, 150, true, 3);
        click(desktop, input, PointI{5, 5}, 200);
        VISOR_CHECK_EQ(spy.doubles, 0);
        click(desktop, input, PointI{5, 5}, 250);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }

    // No clock means no double click, even though now_ms carries numbers. Once
    // a host clock is available the next pair works normally.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100, false);
        click(desktop, input, PointI{5, 5}, 200, false);
        VISOR_CHECK_EQ(spy.doubles, 0);
        click(desktop, input, PointI{5, 5}, 300, true);
        click(desktop, input, PointI{5, 5}, 400, true);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }

    // Unsigned subtraction carries the comparison across the clock wrap.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        const std::uint64_t near_end = std::numeric_limits<std::uint64_t>::max() - 100;
        click(desktop, input, PointI{5, 5}, near_end);
        click(desktop, input, PointI{5, 5}, 200);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }

    // A release outside the owner is a drag, not a click. The owner still
    // hears the release, but the pending pair is cleared.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        visor::ui::dispatch_event(desktop, input,
                                  Event{visor::host::PointerDown{.button = 1, .pos = PointI{5, 5}}},
                                  100, true);
        visor::ui::dispatch_event(desktop, input,
                                  Event{visor::host::PointerUp{.button = 1, .pos = PointI{15, 15}}},
                                  100, true);
        VISOR_CHECK_EQ(spy.ups, 1);
        click(desktop, input, PointI{5, 5}, 200);
        click(desktop, input, PointI{5, 5}, 300);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }

    // A press that hits nobody breaks the pending pair too, even though no
    // widget hears it.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        visor::ui::dispatch_event(
            desktop, input, Event{visor::host::PointerDown{.button = 1, .pos = PointI{50, 50}}},
            150, true);
        visor::ui::dispatch_event(desktop, input,
                                  Event{visor::host::PointerUp{.button = 1, .pos = PointI{50, 50}}},
                                  150, true);
        click(desktop, input, PointI{5, 5}, 200);
        VISOR_CHECK_EQ(spy.doubles, 0);
        click(desktop, input, PointI{5, 5}, 250);
        VISOR_CHECK_EQ(spy.doubles, 1);
    }

    // The callback carries the second press's modifiers, not the first's.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100, true, 1, Modifiers{.shift = true});
        click(desktop, input, PointI{5, 5}, 200, true, 1, Modifiers{.alt = true});
        VISOR_CHECK_EQ(spy.doubles, 1);
        VISOR_CHECK(spy.last_modifiers.alt);
        VISOR_CHECK(!spy.last_modifiers.shift);
    }

    // Identity is assigned once and differs between live widgets.
    {
        const ClickSpy first(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        const ClickSpy second(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 10, .h = 10}});
        VISOR_CHECK(first.id() != second.id());
        VISOR_CHECK_EQ(first.id(), first.id());
    }

    // Identity is stable and unique, and the pending click is keyed by it.
    // A widget removed after the first click, then a new widget at the same
    // tree position, must not inherit the old pair.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        InputRouter input;
        {
            ClickSpy first(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
            VISOR_CHECK(desktop.add_child(&first));
            click(desktop, input, PointI{5, 5}, 100);
            VISOR_CHECK(desktop.remove_child(&first));
        }
        {
            ClickSpy second(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
            VISOR_CHECK(desktop.add_child(&second));
            click(desktop, input, PointI{5, 5}, 200);
            VISOR_CHECK_EQ(second.doubles, 0);
            click(desktop, input, PointI{5, 5}, 250);
            VISOR_CHECK_EQ(second.doubles, 1);
        }
    }

    // Three rapid clicks make one double click, not two. The third click
    // starts a fresh pair, so a fourth click pairs with it.
    {
        visor::ui::Rectangle desktop(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        ClickSpy spy(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}});
        VISOR_CHECK(desktop.add_child(&spy));
        InputRouter input;
        click(desktop, input, PointI{5, 5}, 100);
        click(desktop, input, PointI{5, 5}, 200);
        click(desktop, input, PointI{5, 5}, 300);
        VISOR_CHECK_EQ(spy.ups, 3);
        VISOR_CHECK_EQ(spy.doubles, 1);
        click(desktop, input, PointI{5, 5}, 400);
        VISOR_CHECK_EQ(spy.doubles, 2);
    }
}
