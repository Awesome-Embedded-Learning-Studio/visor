#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/bindings/offscreen/offscreen_host.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <cstddef>
#include <vector>

namespace {

/// A control that records every move it receives: delivery is the whole
/// question, the log is the whole answer.
class MoveSpy final : public visor::ui::Widget {
  public:
    MoveSpy(visor::base::RectI bounds, std::vector<visor::base::PointI>& log) noexcept
        : Widget{bounds}, log_{log} {}

    void on_pointer_move(visor::base::PointI at) noexcept override { log_.push_back(at); }

  protected:
    void on_draw([[maybe_unused]] const visor::ui::Painter& painter) const noexcept override {}

  private:
    std::vector<visor::base::PointI>& log_;
};

} // namespace

void run_gesture_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::Canvas;

    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}};
    const RectI button_bounds{.pos = PointI{2, 2}, .size = SizeI{.w = 6, .h = 4}};

    // Capture closes the stranded press (flipped from the pre-gesture
    // behavior): the release follows the press's owner wherever it lands, so
    // the button hears the outside release, lets the visual go -- and a
    // release outside its bounds is a cancelled click, not a fired one.
    {
        int fired = 0;
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Button button(button_bounds);
        button.set_on_activate(
            [](visor::ui::Button&, void* context) { ++*static_cast<int*>(context); }, &fired);
        VISOR_CHECK(panel.add_child(&button));
        visor::host::OffscreenHost screen(20, 20);
        visor::ui::InputRouter input;
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 4}}));
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{15, 15}}));
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 2);
        VISOR_CHECK(!button.pressed());
        VISOR_CHECK_EQ(fired, 0);
        VISOR_CHECK_EQ(screen.get(PointI{4, 4}), true);
    }

    // Move delivery, the gesture layer's own vocabulary: without a press the
    // move goes to whoever sits on top at the point (hover's foundation);
    // with a press it goes to the press's owner even outside the owner's
    // bounds (drag's foundation), and the release ends the ownership.
    {
        std::vector<PointI> ground_log;
        std::vector<PointI> top_log;
        MoveSpy ground(root_bounds, ground_log);
        MoveSpy lid(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 10, .h = 20}}, top_log);
        VISOR_CHECK(ground.add_child(&lid));
        visor::host::OffscreenHost screen(20, 20);
        visor::ui::InputRouter input;
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;

        // Hover: no press held, a move over the lid is the lid's.
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{15, 5}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, ground, input, frame), 1);
        VISOR_CHECK_EQ(top_log.size(), std::size_t{1});
        VISOR_CHECK_EQ(top_log[0], (PointI{15, 5}));
        VISOR_CHECK_EQ(ground_log.size(), std::size_t{0});
        // A move over uncovered ground is the ground's.
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{5, 5}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, ground, input, frame), 1);
        VISOR_CHECK_EQ(ground_log.size(), std::size_t{1});
        VISOR_CHECK_EQ(top_log.size(), std::size_t{1});

        // Drag: press on the lid, then walk off it -- every move stays the
        // lid's, the ground beneath hears nothing.
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{15, 5}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, ground, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{5, 15}}));
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{2, 2}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, ground, input, frame), 2);
        VISOR_CHECK_EQ(top_log.size(), std::size_t{3});
        VISOR_CHECK_EQ(top_log[2], (PointI{2, 2}));
        VISOR_CHECK_EQ(ground_log.size(), std::size_t{1});
        // The release ends the ownership: moves are hover-routed again.
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{2, 2}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, ground, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{5, 15}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, ground, input, frame), 1);
        VISOR_CHECK_EQ(ground_log.size(), std::size_t{2});
        VISOR_CHECK_EQ(top_log.size(), std::size_t{3});
    }

    // Click synthesis, the return-home case: a press on the button, a wander
    // outside, and a release back inside the bounds fires -- ownership held
    // the whole way, the release landed home, the click counts.
    {
        int fired = 0;
        visor::ui::Rectangle panel(root_bounds);
        visor::ui::Button button(button_bounds);
        button.set_on_activate(
            [](visor::ui::Button&, void* context) { ++*static_cast<int*>(context); }, &fired);
        VISOR_CHECK(panel.add_child(&button));
        visor::host::OffscreenHost screen(20, 20);
        visor::ui::InputRouter input;
        const auto host = screen.table();
        Canvas<SizeI{.w = 20, .h = 20}> frame;
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{15, 15}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{15, 15}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(!button.pressed());
        VISOR_CHECK_EQ(fired, 0); // the wander-out release cancelled that click
        VISOR_CHECK(screen.push_event(visor::host::PointerDown{.button = 1, .pos = PointI{4, 4}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{15, 15}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerMove{.pos = PointI{4, 5}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK(screen.push_event(visor::host::PointerUp{.button = 1, .pos = PointI{4, 5}}));
        VISOR_CHECK_EQ(visor::ui::run_once(host, panel, input, frame), 1);
        VISOR_CHECK_EQ(fired, 1); // home again: the click counts
    }
}
