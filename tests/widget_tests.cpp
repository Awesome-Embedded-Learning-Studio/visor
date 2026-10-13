#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"
#include "visor/draw/mono_bitmap.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/primitives/rectangle/rectangle.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace {

/// A Painter double: records strokes instead of touching pixels.
class RecordingPainter final : public visor::ui::Painter {
  public:
    struct Fill {
        visor::base::RectI area;
        visor::canvas::Color ink;
    };
    struct Plotted {
        visor::base::PointI at;
        visor::canvas::Color ink;
    };
    struct BitmapDrawn {
        visor::base::PointI at;
        int scale = 1;
    };

    void fill(visor::base::RectI area, visor::canvas::Color ink) const noexcept override {
        fills_.push_back(Fill{.area = area, .ink = ink});
    }
    void plot(visor::base::PointI p, visor::canvas::Color ink) const noexcept override {
        plots_.push_back(Plotted{.at = p, .ink = ink});
    }
    void draw_bitmap(visor::base::PointI at, const visor::draw::MonoBitmap& source,
                     visor::canvas::Color ink, int scale) const noexcept override {
        static_cast<void>(source);
        static_cast<void>(ink);
        bitmaps_.push_back(BitmapDrawn{.at = at, .scale = scale});
    }

    [[nodiscard]] const std::vector<Fill>& fills() const noexcept { return fills_; }
    [[nodiscard]] const std::vector<Plotted>& plots() const noexcept { return plots_; }
    [[nodiscard]] const std::vector<BitmapDrawn>& bitmaps() const noexcept { return bitmaps_; }

  private:
    mutable std::vector<Fill> fills_;
    mutable std::vector<Plotted> plots_;
    mutable std::vector<BitmapDrawn> bitmaps_;
};

/// A minimal widget that records its draw order instead of painting.
class Tracer final : public visor::ui::Widget {
  public:
    Tracer(visor::base::RectI bounds, int tag, std::vector<int>& order) noexcept
        : Widget{bounds}, tag_{tag}, order_{order} {}

  protected:
    void on_draw([[maybe_unused]] const visor::ui::Painter& painter) const noexcept override {
        order_.push_back(tag_);
    }

  private:
    int tag_;
    std::vector<int>& order_;
};

/// Shared draw-order log; every Tracer appends its tag when painted.
std::vector<int> widget_order;

} // namespace

void run_widget_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::ui::Widget;

    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}};
    const RectI left_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}};

    // Pre-order drawing: parent first, then children in add order, grandchildren
    // right after their own parent, later siblings after that.
    Tracer root(root_bounds, 0, widget_order);
    Tracer left(left_bounds, 1, widget_order);
    Tracer lefts_child(left_bounds, 2, widget_order);
    Tracer right(left_bounds, 3, widget_order);
    VISOR_CHECK(root.add_child(&left));
    VISOR_CHECK(left.add_child(&lefts_child));
    VISOR_CHECK(root.add_child(&right));
    const RecordingPainter painter;
    root.draw(painter);
    const std::array<int, 4> expected_order{0, 1, 2, 3};
    VISOR_CHECK_EQ(widget_order.size(), expected_order.size());
    for (std::size_t i = 0; i < expected_order.size(); ++i) {
        VISOR_CHECK_EQ(widget_order[i], expected_order[i]);
    }

    // Hit testing walks the tree the other way: the last-added overlapping
    // child wins, a point on the parent but off every child stays with the
    // parent, and a point outside the root finds nothing.
    VISOR_CHECK_EQ(root.hit_test((PointI{5, 5})), static_cast<Widget*>(&right));
    VISOR_CHECK_EQ(root.hit_test((PointI{15, 15})), static_cast<Widget*>(&root));
    VISOR_CHECK_EQ(root.hit_test((PointI{25, 5})), static_cast<Widget*>(nullptr));
    // left's own child covers the same area and was added later, so it wins.
    VISOR_CHECK_EQ(left.hit_test((PointI{5, 5})), static_cast<Widget*>(&lefts_child));

    // An invisible widget neither draws nor hits, and hides its subtree too.
    widget_order.clear();
    left.set_visible(false);
    root.draw(painter);
    const std::array<int, 2> expected_hidden{0, 3};
    VISOR_CHECK_EQ(widget_order.size(), expected_hidden.size());
    for (std::size_t i = 0; i < expected_hidden.size(); ++i) {
        VISOR_CHECK_EQ(widget_order[i], expected_hidden[i]);
    }
    VISOR_CHECK_EQ(root.hit_test((PointI{5, 5})), static_cast<Widget*>(&right));
    left.set_visible(true);

    // The root's own switch turns off the whole tree at once.
    widget_order.clear();
    root.set_visible(false);
    root.draw(painter);
    VISOR_CHECK(widget_order.empty());
    VISOR_CHECK_EQ(root.hit_test((PointI{5, 5})), static_cast<Widget*>(nullptr));
    root.set_visible(true);

    // Child capacity is a compile-time fact: the child past kMaxChildren is
    // refused, the tree keeps the ones it took.
    Tracer hoard(root_bounds, 9, widget_order);
    std::size_t taken = 0;
    for (std::size_t i = 0; i < visor::ui::kMaxChildren + 1; ++i) {
        Tracer slot(root_bounds, 100 + static_cast<int>(i), widget_order);
        if (hoard.add_child(&slot)) {
            ++taken;
        }
    }
    VISOR_CHECK_EQ(taken, visor::ui::kMaxChildren);
    VISOR_CHECK_EQ(hoard.child_count(), visor::ui::kMaxChildren);

    // Rectangle paints exactly its own bounds with its ink, nothing else.
    visor::ui::Rectangle rect(left_bounds);
    rect.fill = visor::canvas::Color::Off;
    rect.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 1U);
    VISOR_CHECK_EQ(painter.fills()[0].area, left_bounds);
    VISOR_CHECK_EQ(painter.fills()[0].ink, visor::canvas::Color::Off);

    // Button: the state flip and the ink flip agree on both edges of a press.
    visor::ui::Button button(left_bounds);
    VISOR_CHECK(!button.pressed());
    button.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 2U);
    VISOR_CHECK_EQ(painter.fills()[1].ink, visor::canvas::Color::On);
    button.on_pointer_down(PointI{5, 5});
    VISOR_CHECK(button.pressed());
    button.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 3U);
    VISOR_CHECK_EQ(painter.fills()[2].ink, visor::canvas::Color::Off);
    button.on_pointer_up(PointI{5, 5});
    VISOR_CHECK(!button.pressed());
    button.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 4U);
    VISOR_CHECK_EQ(painter.fills()[3].ink, visor::canvas::Color::On);

    // The same story on real pixels: unpressed lights its bounds, pressed
    // clears them again.
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> frame;
    const visor::ui::CanvasPainter frame_painter{frame};
    frame.set_clear(visor::canvas::Color::Off);
    button.draw(frame_painter);
    VISOR_CHECK(frame.get(PointI{4, 4}) == visor::canvas::Color::On);
    button.on_pointer_down(PointI{5, 5});
    frame.set_clear(visor::canvas::Color::Off);
    button.draw(frame_painter);
    VISOR_CHECK(frame.get(PointI{4, 4}) == visor::canvas::Color::Off);

    // Replay contract: a panel with a pressed button on top, drawn twice,
    // must leave identical bytes -- draw reads state and touches nothing.
    visor::ui::Rectangle panel(root_bounds);
    visor::ui::Button redial(left_bounds);
    VISOR_CHECK(panel.add_child(&redial));
    redial.on_pointer_down(PointI{5, 5});
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> replay_first;
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> replay_second;
    replay_first.set_clear(visor::canvas::Color::Off);
    replay_second.set_clear(visor::canvas::Color::Off);
    panel.draw(visor::ui::CanvasPainter{replay_first});
    panel.draw(visor::ui::CanvasPainter{replay_second});
    VISOR_CHECK(std::equal(replay_first.pixels().begin(), replay_first.pixels().end(),
                           replay_second.pixels().begin()));

    // Focus: the tree holds it, the walk hands it on. Tab order is draw order,
    // the walk wraps past the last one, and hidden branches take no part.
    visor::ui::Rectangle focus_panel(root_bounds);
    visor::ui::Button alpha(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}});
    visor::ui::Button beta(RectI{.pos = PointI{6, 0}, .size = SizeI{.w = 4, .h = 4}});
    visor::ui::Button gamma(RectI{.pos = PointI{12, 0}, .size = SizeI{.w = 4, .h = 4}});
    VISOR_CHECK(focus_panel.add_child(&alpha));
    VISOR_CHECK(focus_panel.add_child(&beta));
    VISOR_CHECK(focus_panel.add_child(&gamma));
    VISOR_CHECK(!alpha.focused());
    VISOR_CHECK(alpha.focusable());
    VISOR_CHECK(focus_panel.advance_focus());
    VISOR_CHECK(alpha.focused());
    VISOR_CHECK(focus_panel.advance_focus());
    VISOR_CHECK(beta.focused());
    VISOR_CHECK(!alpha.focused());
    VISOR_CHECK(focus_panel.advance_focus());
    VISOR_CHECK(gamma.focused());
    // Wrap: past the last one, the first takes it back.
    VISOR_CHECK(focus_panel.advance_focus());
    VISOR_CHECK(alpha.focused());
    VISOR_CHECK(!gamma.focused());
    // A hidden branch is skipped in traversal too.
    beta.set_visible(false);
    VISOR_CHECK(focus_panel.advance_focus());
    VISOR_CHECK(gamma.focused());
    // The holder itself hidden: focus moves on to the first visible focusable.
    gamma.set_visible(false);
    VISOR_CHECK(focus_panel.advance_focus());
    VISOR_CHECK(alpha.focused());
    VISOR_CHECK(!gamma.focused());
    gamma.set_visible(true);
    // A tree without a single focusable widget refuses and stays put.
    visor::ui::Rectangle bare(left_bounds);
    VISOR_CHECK(!bare.advance_focus());

    // The focus ring on real pixels: the holder's border turns dark, its
    // interior stays lit, and dropping the focus restores the plain fill.
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> ring_frame;
    const visor::ui::CanvasPainter ring_painter{ring_frame};
    ring_frame.set_clear(visor::canvas::Color::Off);
    alpha.set_focused(true);
    alpha.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == visor::canvas::Color::Off);
    VISOR_CHECK(ring_frame.get(PointI{2, 2}) == visor::canvas::Color::On);
    alpha.set_focused(false);
    ring_frame.set_clear(visor::canvas::Color::Off);
    alpha.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == visor::canvas::Color::On);

    // Checkbox: the third control and the first toggler. Press flips, checked
    // paints the box solid, unchecked leaves it hollow, and the box keeps two
    // lit rows for ring and margin.
    visor::ui::Checkbox toggle(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 8, .h = 8}});
    VISOR_CHECK(!toggle.checked());
    toggle.on_pointer_down(PointI{5, 5});
    VISOR_CHECK(toggle.checked());
    toggle.on_pointer_down(PointI{5, 5});
    VISOR_CHECK(!toggle.checked());
    toggle.on_pointer_down(PointI{5, 5}); // stays checked for the pixel assertions
    // The background belongs to the scene, not the control: the checkbox only
    // paints its box, so the lit margins here come from the clear color.
    ring_frame.set_clear(visor::canvas::Color::On);
    toggle.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == visor::canvas::Color::On);  // no focus, no ring
    VISOR_CHECK(ring_frame.get(PointI{2, 2}) == visor::canvas::Color::Off); // box frame
    VISOR_CHECK(ring_frame.get(PointI{4, 4}) == visor::canvas::Color::Off); // solid mark
    toggle.on_pointer_down(PointI{5, 5});
    ring_frame.set_clear(visor::canvas::Color::On);
    toggle.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{2, 2}) == visor::canvas::Color::Off); // frame stays
    VISOR_CHECK(ring_frame.get(PointI{4, 4}) == visor::canvas::Color::On);  // hollow interior
    // Focused and checked: ring, lit margin, box frame, mark, each on its own row.
    toggle.on_pointer_down(PointI{5, 5});
    toggle.set_focused(true);
    ring_frame.set_clear(visor::canvas::Color::On);
    toggle.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == visor::canvas::Color::Off); // ring
    VISOR_CHECK(ring_frame.get(PointI{1, 1}) == visor::canvas::Color::On);  // lit margin
    VISOR_CHECK(ring_frame.get(PointI{2, 2}) == visor::canvas::Color::Off); // box frame
    VISOR_CHECK(ring_frame.get(PointI{4, 4}) == visor::canvas::Color::Off); // mark

    // The replay promise now covers focus and a toggled control too: a panel
    // with a pressed button and a checked, focused checkbox, drawn twice,
    // still leaves identical bytes.
    visor::ui::Checkbox replay_toggle(left_bounds);
    VISOR_CHECK(panel.add_child(&replay_toggle));
    replay_toggle.on_pointer_down(PointI{5, 5});
    replay_toggle.set_focused(true);
    panel.draw(visor::ui::CanvasPainter{replay_first});
    panel.draw(visor::ui::CanvasPainter{replay_second});
    VISOR_CHECK(std::equal(replay_first.pixels().begin(), replay_first.pixels().end(),
                           replay_second.pixels().begin()));

    // The report lane: state changes push their owed areas into the collector
    // the tree was wired with. Wiring after assembly reaches the whole subtree,
    // and an adoptee inherits the sink of the parent it joins.
    visor::ui::DirtyRegions reports;
    visor::ui::Rectangle wired_panel(root_bounds);
    visor::ui::Button wired_one(left_bounds);
    VISOR_CHECK(wired_panel.add_child(&wired_one));
    wired_panel.set_dirty_sink(&reports);
    wired_one.on_pointer_down(PointI{5, 5}); // a control reports its own bounds
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    VISOR_CHECK_EQ(reports.at(0), left_bounds);
    reports.clear();
    wired_panel.set_visible(false); // hiding owes the old pixels
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    VISOR_CHECK_EQ(reports.at(0), root_bounds);
    wired_panel.set_visible(true); // the same area again: merged, not duplicated
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    VISOR_CHECK_EQ(reports.at(0), root_bounds);
    wired_panel.set_visible(true); // no flip, no pixel change, no report
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    reports.clear();
    visor::ui::Button late(RectI{.pos = PointI{12, 12}, .size = SizeI{.w = 4, .h = 4}});
    VISOR_CHECK(wired_panel.add_child(&late)); // the adoptee inherits the sink
    late.on_pointer_down(PointI{5, 5});
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    VISOR_CHECK_EQ(reports.at(0), late.bounds());
    reports.clear();
    wired_one.set_focused(true); // the ring's area is a debt like any other
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    VISOR_CHECK_EQ(reports.at(0), left_bounds);
    wired_one.set_focused(true); // already holding: nothing changes
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    wired_one.set_focused(false);
    VISOR_CHECK_EQ(reports.count(), std::size_t{1}); // the same box, merged again
}
