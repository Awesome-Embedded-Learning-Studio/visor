#include "cases.hpp"
#include "testing.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/popup_layer.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::Modifiers;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::host::PointerWheel;
using visor::ui::InputRouter;
using visor::ui::PopupLayer;
using visor::ui::Widget;

class NullPainter final : public visor::ui::Painter {
  public:
    void fill([[maybe_unused]] RectI area,
              [[maybe_unused]] visor::canvas::ColorRole ink) const noexcept override {}
    void plot([[maybe_unused]] PointI at,
              [[maybe_unused]] visor::canvas::ColorRole ink) const noexcept override {}
    void draw_bitmap([[maybe_unused]] PointI at,
                     [[maybe_unused]] const visor::draw::MonoBitmap& source,
                     [[maybe_unused]] visor::canvas::ColorRole ink,
                     [[maybe_unused]] int scale) const noexcept override {}
    void
    fill_rounded([[maybe_unused]] RectI area, [[maybe_unused]] visor::canvas::ColorRole ink,
                 [[maybe_unused]] const visor::ui::CornerRadius& radius) const noexcept override {}
};

/// A content double: records what reached it and where it painted in the
/// draw order.
class Spy final : public Widget {
  public:
    Spy(RectI bounds, int tag, std::vector<int>& draw_order) noexcept
        : Widget{bounds}, tag_{tag}, order_{draw_order} {}

    void on_pointer_down(PointI at, [[maybe_unused]] Modifiers modifiers) noexcept override {
        ++downs;
        last_down = at;
    }
    void on_pointer_up(PointI at) noexcept override {
        ++ups;
        last_up = at;
    }
    void on_pointer_move(PointI at) noexcept override {
        ++moves;
        last_move = at;
    }
    void on_pointer_wheel([[maybe_unused]] const PointerWheel& wheel) noexcept override {
        ++wheels;
    }

    int downs = 0;
    int ups = 0;
    int moves = 0;
    int wheels = 0;
    PointI last_down;
    PointI last_up;
    PointI last_move;

  protected:
    void on_draw([[maybe_unused]] const visor::ui::Painter& painter) const noexcept override {
        order_.push_back(tag_);
    }

  private:
    int tag_;
    std::vector<int>& order_;
};

struct DismissLog {
    int count = 0;
    Widget* content = nullptr;
};

void log_dismiss([[maybe_unused]] PopupLayer& layer, Widget& content, void* context) noexcept {
    auto& log = *static_cast<DismissLog*>(context);
    ++log.count;
    log.content = &content;
}

void click(Widget& root, InputRouter& input, PointI at, std::uint64_t now = 0) noexcept {
    visor::ui::dispatch_event(root, input, Event{PointerDown{.button = 1, .pos = at}}, now, true);
    visor::ui::dispatch_event(root, input, Event{PointerUp{.button = 1, .pos = at}}, now, true);
}

void move(Widget& root, InputRouter& input, PointI at) noexcept {
    visor::ui::dispatch_event(root, input, Event{PointerMove{.pos = at}}, 0, true);
}

void wheel(Widget& root, InputRouter& input, PointI at) noexcept {
    visor::ui::dispatch_event(
        root, input, Event{PointerWheel{.dx = 1, .dy = 0, .pos = at, .modifiers = {}}}, 0, true);
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_popup_tests() {
    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 40, .h = 40}};

    // Construction hides the layer. open adopts the content, raises the
    // layer above a later sibling, and the full screen becomes the layer's
    // hit surface: inside the content the content wins, outside the content
    // the layer itself wins even over a sibling that paints later.
    {
        std::vector<int> order;
        visor::ui::Rectangle desktop(root_bounds);
        Spy background(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}}, 1, order);
        Spy menu(RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 10, .h = 10}}, 2, order);
        Spy top(RectI{.pos = PointI{30, 30}, .size = SizeI{.w = 10, .h = 10}}, 3, order);
        PopupLayer layer(root_bounds);
        VISOR_CHECK(desktop.add_child(&background));
        VISOR_CHECK(desktop.add_child(&layer));
        VISOR_CHECK(desktop.add_child(&top));
        VISOR_CHECK(!layer.visible());
        VISOR_CHECK(!layer.is_open());
        VISOR_CHECK(layer.open(&menu));
        VISOR_CHECK(layer.is_open());
        VISOR_CHECK(layer.visible());
        VISOR_CHECK_EQ(layer.content(), &menu);
        VISOR_CHECK_EQ(layer.child_count(), std::size_t{1});
        VISOR_CHECK_EQ(layer.child(0), static_cast<Widget*>(&menu));

        const NullPainter painter;
        order.clear();
        desktop.draw(painter);
        const std::array<int, 3> expected{1, 3, 2};
        VISOR_CHECK_EQ(order.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            VISOR_CHECK_EQ(order[i], expected[i]);
        }

        VISOR_CHECK_EQ(desktop.hit_test(PointI{25, 25}), static_cast<Widget*>(&menu));
        VISOR_CHECK_EQ(desktop.hit_test(PointI{35, 35}), static_cast<Widget*>(&layer));
        VISOR_CHECK_EQ(desktop.hit_test(PointI{5, 5}), static_cast<Widget*>(&layer));
    }

    // An outside press closes and consumes: the underlying background and
    // the menu hear nothing, the dismiss callback sees the detached content,
    // and focus stays where it was because the layer is not focusable.
    {
        std::vector<int> order;
        visor::ui::Rectangle desktop(root_bounds);
        visor::ui::Button background(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        Spy menu(RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 10, .h = 10}}, 2, order);
        PopupLayer layer(root_bounds);
        VISOR_CHECK(desktop.add_child(&background));
        VISOR_CHECK(desktop.add_child(&layer));
        background.set_focused(true);
        DismissLog log;
        layer.set_on_dismiss(log_dismiss, &log);
        VISOR_CHECK(layer.open(&menu));
        InputRouter input;
        click(desktop, input, PointI{5, 5});
        VISOR_CHECK(!layer.is_open());
        VISOR_CHECK_EQ(layer.content(), static_cast<Widget*>(nullptr));
        VISOR_CHECK_EQ(layer.child_count(), std::size_t{0});
        VISOR_CHECK_EQ(log.count, 1);
        VISOR_CHECK_EQ(log.content, static_cast<Widget*>(&menu));
        VISOR_CHECK_EQ(menu.downs, 0);
        VISOR_CHECK_EQ(menu.ups, 0);
        VISOR_CHECK(background.focused());
        VISOR_CHECK_EQ(desktop.hit_test(PointI{5, 5}), static_cast<Widget*>(&background));

        // The layer is closed: a second press is ordinary background input
        // and the dismiss callback does not fire again.
        click(desktop, input, PointI{5, 5});
        VISOR_CHECK_EQ(log.count, 1);
    }

    // A press inside the content goes through the ordinary route and leaves
    // the popup open.
    {
        std::vector<int> order;
        visor::ui::Rectangle desktop(root_bounds);
        Spy menu(RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 10, .h = 10}}, 2, order);
        PopupLayer layer(root_bounds);
        VISOR_CHECK(desktop.add_child(&layer));
        DismissLog log;
        layer.set_on_dismiss(log_dismiss, &log);
        VISOR_CHECK(layer.open(&menu));
        InputRouter input;
        click(desktop, input, PointI{25, 25});
        VISOR_CHECK(layer.is_open());
        VISOR_CHECK_EQ(menu.downs, 1);
        VISOR_CHECK_EQ(menu.ups, 1);
        VISOR_CHECK_EQ(menu.last_down, (PointI{25, 25}));
        VISOR_CHECK_EQ(log.count, 0);
    }

    // Moving and wheeling outside the content stop at the layer; inside the
    // content they reach it. The popup is not closed by either.
    {
        std::vector<int> order;
        visor::ui::Rectangle desktop(root_bounds);
        Spy background(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}}, 1, order);
        Spy menu(RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 10, .h = 10}}, 2, order);
        PopupLayer layer(root_bounds);
        VISOR_CHECK(desktop.add_child(&background));
        VISOR_CHECK(desktop.add_child(&layer));
        VISOR_CHECK(layer.open(&menu));
        InputRouter input;

        move(desktop, input, PointI{5, 5});
        VISOR_CHECK_EQ(background.moves, 0);
        move(desktop, input, PointI{25, 25});
        VISOR_CHECK_EQ(menu.moves, 1);

        wheel(desktop, input, PointI{5, 5});
        VISOR_CHECK_EQ(background.wheels, 0);
        wheel(desktop, input, PointI{25, 25});
        VISOR_CHECK_EQ(menu.wheels, 1);
        VISOR_CHECK(layer.is_open());
    }

    // open/close contract: null, self, already open, and an already-parented
    // content are refused. close returns the content and is idempotent, and
    // an explicit close does not fire the dismiss callback.
    {
        std::vector<int> order;
        visor::ui::Rectangle desktop(root_bounds);
        Spy menu(RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 10, .h = 10}}, 2, order);
        PopupLayer layer(root_bounds);
        VISOR_CHECK(desktop.add_child(&layer));
        DismissLog log;
        layer.set_on_dismiss(log_dismiss, &log);

        VISOR_CHECK(!layer.open(nullptr));
        VISOR_CHECK(!layer.open(&layer));
        VISOR_CHECK(layer.open(&menu));
        VISOR_CHECK(!layer.open(&menu)); // already open
        VISOR_CHECK_EQ(layer.close(), static_cast<Widget*>(&menu));
        VISOR_CHECK(!layer.is_open());
        VISOR_CHECK_EQ(layer.content(), static_cast<Widget*>(nullptr));
        VISOR_CHECK_EQ(log.count, 0); // explicit close is not a dismissal
        VISOR_CHECK_EQ(layer.close(), static_cast<Widget*>(nullptr));

        VISOR_CHECK(layer.open(&menu)); // re-open after detach
        VISOR_CHECK_EQ(layer.close(), static_cast<Widget*>(&menu));
        VISOR_CHECK(desktop.add_child(&menu)); // menu now lives under the desktop
        VISOR_CHECK(!layer.open(&menu));       // a parented content is refused
        VISOR_CHECK(!layer.is_open());
    }

    // Opening and closing report the whole screen, the first version's
    // documented price. The content's own boxes merge into that report.
    {
        visor::ui::Rectangle desktop(root_bounds);
        visor::ui::DirtyRegions reports;
        desktop.set_dirty_sink(&reports);
        std::vector<int> order;
        Spy menu(RectI{.pos = PointI{20, 20}, .size = SizeI{.w = 10, .h = 10}}, 2, order);
        PopupLayer layer(root_bounds);
        VISOR_CHECK(desktop.add_child(&layer));
        reports.clear();

        VISOR_CHECK(layer.open(&menu));
        VISOR_CHECK_EQ(reports.count(), std::size_t{1});
        VISOR_CHECK_EQ(reports.at(0), root_bounds);

        reports.clear();
        VISOR_CHECK_EQ(layer.close(), static_cast<Widget*>(&menu));
        VISOR_CHECK_EQ(reports.count(), std::size_t{1});
        VISOR_CHECK_EQ(reports.at(0), root_bounds);
    }
}
