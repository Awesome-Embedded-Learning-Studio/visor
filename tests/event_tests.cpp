#include "cases.hpp"
#include "testing.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <cstdint>
#include <variant>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::KeyDown;
using visor::host::KeyUp;
using visor::host::Modifiers;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::host::PointerWheel;
using visor::ui::InputRouter;

class Probe final : public visor::ui::Widget {
  public:
    using Widget::Widget;

    [[nodiscard]] bool focusable() const noexcept override { return true; }

    void on_key_down(std::uint32_t code, Modifiers modifiers) noexcept override {
        last_key = code;
        key_modifiers = modifiers;
        ++key_downs;
    }

    void on_key_up(std::uint32_t code, Modifiers modifiers) noexcept override {
        last_release = code;
        release_modifiers = modifiers;
        ++key_ups;
    }

    void on_pointer_down(PointI at, Modifiers modifiers) noexcept override {
        last_down = at;
        down_modifiers = modifiers;
        ++pointer_downs;
    }

    void on_pointer_up(PointI at) noexcept override {
        last_up = at;
        ++pointer_ups;
    }

    void on_pointer_move(PointI at) noexcept override {
        last_move = at;
        ++pointer_moves;
    }

    void on_pointer_wheel(const PointerWheel& wheel) noexcept override {
        last_wheel = wheel;
        ++wheels;
    }

    int key_downs = 0;
    int key_ups = 0;
    int pointer_downs = 0;
    int pointer_ups = 0;
    int pointer_moves = 0;
    int wheels = 0;
    std::uint32_t last_key = 0;
    std::uint32_t last_release = 0;
    PointI last_down;
    PointI last_up;
    PointI last_move;
    PointerWheel last_wheel{};
    Modifiers key_modifiers{};
    Modifiers release_modifiers{};
    Modifiers down_modifiers{};

  protected:
    void on_draw([[maybe_unused]] const visor::ui::Painter& painter) const noexcept override {}
};

} // namespace

void run_event_tests() {
    static_assert(std::variant_size_v<Event> == 6);

    Probe root(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 100, .h = 100}});
    Probe target(RectI{.pos = PointI{10, 10}, .size = SizeI{.w = 20, .h = 20}});
    Probe second(RectI{.pos = PointI{40, 40}, .size = SizeI{.w = 20, .h = 20}});
    VISOR_CHECK(root.add_child(&target));
    VISOR_CHECK(root.add_child(&second));

    InputRouter input;

    // A wheel is a hover word: it reaches the topmost widget under the point.
    visor::ui::dispatch_event(root, input,
                              Event{PointerWheel{
                                  .dx = 1,
                                  .dy = -2,
                                  .pos = PointI{15, 15},
                                  .modifiers = Modifiers{.alt = true},
                              }},
                              0, true);
    VISOR_CHECK_EQ(target.wheels, 1);
    VISOR_CHECK_EQ(root.wheels, 0);
    VISOR_CHECK_EQ(target.last_wheel.dx, 1);
    VISOR_CHECK_EQ(target.last_wheel.dy, -2);
    VISOR_CHECK_EQ(target.last_wheel.pos, (PointI{15, 15}));
    VISOR_CHECK(target.last_wheel.modifiers.alt);

    // A press names the owner and the focus holder. The wheel still follows
    // the pointer, so a wheel over the parent reaches the parent while the
    // press stays with the child.
    visor::ui::dispatch_event(
        root, input,
        Event{
            PointerDown{.button = 1, .pos = PointI{15, 15}, .modifiers = Modifiers{.shift = true}}},
        10, true);
    VISOR_CHECK_EQ(input.press_owner(), &target);
    VISOR_CHECK_EQ(target.pointer_downs, 1);
    VISOR_CHECK(target.down_modifiers.shift);
    VISOR_CHECK_EQ(root.focused_widget(), &target);

    visor::ui::dispatch_event(
        root, input, Event{PointerWheel{.dx = 0, .dy = 1, .pos = PointI{2, 2}, .modifiers = {}}},
        20, true);
    VISOR_CHECK_EQ(root.wheels, 1);
    VISOR_CHECK_EQ(target.wheels, 1);
    VISOR_CHECK_EQ(input.press_owner(), &target);

    // A wheel that hits nobody is dropped without disturbing the press.
    visor::ui::dispatch_event(
        root, input,
        Event{PointerWheel{.dx = 0, .dy = 1, .pos = PointI{200, 200}, .modifiers = {}}}, 30, true);
    VISOR_CHECK_EQ(root.wheels, 1);
    VISOR_CHECK_EQ(target.wheels, 1);
    VISOR_CHECK_EQ(input.press_owner(), &target);

    // The release still follows the press owner wherever it lands.
    visor::ui::dispatch_event(root, input, Event{PointerUp{.button = 1, .pos = PointI{2, 2}}}, 40,
                              true);
    VISOR_CHECK_EQ(target.pointer_ups, 1);
    VISOR_CHECK_EQ(input.press_owner(), nullptr);

    // Key events reach the focus holder with their modifiers.
    visor::ui::dispatch_event(
        root, input,
        Event{KeyDown{.code = 65, .modifiers = Modifiers{.shift = true, .ctrl = true}}}, 50, true);
    VISOR_CHECK_EQ(target.key_downs, 1);
    VISOR_CHECK_EQ(target.last_key, 65U);
    VISOR_CHECK(target.key_modifiers.ctrl);
    VISOR_CHECK(target.key_modifiers.shift);
    VISOR_CHECK(!target.key_modifiers.alt);

    visor::ui::dispatch_event(
        root, input, Event{KeyUp{.code = 65, .modifiers = Modifiers{.ctrl = true}}}, 60, true);
    VISOR_CHECK_EQ(target.key_ups, 1);
    VISOR_CHECK_EQ(target.last_release, 65U);
    VISOR_CHECK(target.release_modifiers.ctrl);

    // Tab still belongs to the tree, modifiers or not.
    visor::ui::dispatch_event(
        root, input,
        Event{KeyDown{.code = visor::host::kKeyTab, .modifiers = Modifiers{.ctrl = true}}}, 70,
        true);
    VISOR_CHECK_EQ(root.focused_widget(), &second);
    VISOR_CHECK_EQ(second.key_downs, 0);

    // A pointer press with modifiers reaches the new owner, and a move while
    // the press is held goes to that owner, wherever the pointer sits.
    visor::ui::dispatch_event(
        root, input,
        Event{
            PointerDown{.button = 1, .pos = PointI{45, 45}, .modifiers = Modifiers{.super = true}}},
        80, true);
    VISOR_CHECK_EQ(input.press_owner(), &second);
    VISOR_CHECK(second.down_modifiers.super);
    visor::ui::dispatch_event(root, input, Event{PointerMove{.pos = PointI{12, 12}}}, 90, true);
    VISOR_CHECK_EQ(second.pointer_moves, 1);
    VISOR_CHECK_EQ(second.last_move, (PointI{12, 12}));

    // The router owns Tab's focus domain. A window scope keeps the walk inside
    // one subtree; clearing it restores the root domain; a scope that no
    // longer names a member falls back to the root domain.
    {
        visor::ui::Rectangle scoped_root(
            RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 40, .h = 10}});
        visor::ui::Rectangle window_a(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        visor::ui::Rectangle window_b(RectI{.pos = PointI{12, 0}, .size = SizeI{.w = 10, .h = 10}});
        Probe a1(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}});
        Probe a2(RectI{.pos = PointI{6, 0}, .size = SizeI{.w = 4, .h = 4}});
        Probe b1(RectI{.pos = PointI{12, 0}, .size = SizeI{.w = 4, .h = 4}});
        Probe b2(RectI{.pos = PointI{18, 0}, .size = SizeI{.w = 4, .h = 4}});
        VISOR_CHECK(scoped_root.add_child(&window_a));
        VISOR_CHECK(scoped_root.add_child(&window_b));
        VISOR_CHECK(window_a.add_child(&a1));
        VISOR_CHECK(window_a.add_child(&a2));
        VISOR_CHECK(window_b.add_child(&b1));
        VISOR_CHECK(window_b.add_child(&b2));

        InputRouter scoped_input;
        const auto tab = [&scoped_root, &scoped_input](std::uint64_t now) {
            visor::ui::dispatch_event(scoped_root, scoped_input,
                                      Event{KeyDown{.code = visor::host::kKeyTab, .modifiers = {}}},
                                      now, true);
        };

        scoped_input.set_focus_scope(&window_a);
        tab(100);
        VISOR_CHECK(a1.focused());
        tab(110);
        VISOR_CHECK(a2.focused());
        tab(120);
        VISOR_CHECK(a1.focused()); // wrapped inside window A

        scoped_input.set_focus_scope(&window_b);
        tab(130);
        VISOR_CHECK(b1.focused());
        VISOR_CHECK(!a1.focused());
        tab(140);
        VISOR_CHECK(b2.focused());
        tab(150);
        VISOR_CHECK(b1.focused());

        scoped_input.set_focus_scope(nullptr);
        tab(160);
        VISOR_CHECK(b2.focused()); // root domain continues past B's first

        // A scope pointing outside the dispatched tree is ignored, and the
        // root domain takes over again: from B's last, Tab wraps to A's first.
        Probe outsider(RectI{.pos = PointI{30, 0}, .size = SizeI{.w = 4, .h = 4}});
        scoped_input.set_focus_scope(&outsider);
        tab(170);
        VISOR_CHECK(a1.focused());
        VISOR_CHECK(!outsider.focused());
    }
}
