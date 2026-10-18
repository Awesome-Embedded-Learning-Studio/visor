#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
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
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/button/button.hpp"
#include "visor/ui/widgets/checkbox/checkbox.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace {

/// A Painter double: records strokes instead of touching pixels.
class RecordingPainter final : public visor::ui::Painter {
  public:
    struct Fill {
        visor::base::RectI area;
        visor::canvas::ColorRole ink;
    };
    struct Plotted {
        visor::base::PointI at;
        visor::canvas::ColorRole ink;
    };
    struct BitmapDrawn {
        visor::base::PointI at;
        int scale = 1;
    };

    void fill(visor::base::RectI area, visor::canvas::ColorRole ink) const noexcept override {
        fills_.push_back(Fill{.area = area, .ink = ink});
    }
    void plot(visor::base::PointI p, visor::canvas::ColorRole ink) const noexcept override {
        plots_.push_back(Plotted{.at = p, .ink = ink});
    }
    void draw_bitmap(visor::base::PointI at, const visor::draw::MonoBitmap& source,
                     visor::canvas::ColorRole ink, int scale) const noexcept override {
        static_cast<void>(source);
        static_cast<void>(ink);
        bitmaps_.push_back(BitmapDrawn{.at = at, .scale = scale});
    }

    [[nodiscard]] const std::vector<Fill>& fills() const noexcept { return fills_; }
    [[nodiscard]] const std::vector<Plotted>& plots() const noexcept { return plots_; }
    void
    fill_rounded(visor::base::RectI area, visor::canvas::ColorRole ink,
                 [[maybe_unused]] const visor::ui::CornerRadius& radius) const noexcept override {
        fills_.push_back(Fill{.area = area, .ink = ink});
    }

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

void run_z_order_cases() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::ui::Widget;

    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}};
    const RectI left_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}};
    const RecordingPainter painter;

    // Z order: raise() and lower() move a child within its parent's list, and
    // both walks follow. Three children share one box, so the last one in the
    // list paints last and wins the hit. Raising the middle one changes both;
    // lowering it changes both again. Only the moved box owes repaint.
    {
        visor::ui::DirtyRegions stack_reports;
        Tracer stack_root(root_bounds, 20, widget_order);
        Tracer stack_bottom(left_bounds, 21, widget_order);
        Tracer stack_middle(left_bounds, 22, widget_order);
        Tracer stack_top(left_bounds, 23, widget_order);
        VISOR_CHECK(stack_root.add_child(&stack_bottom));
        VISOR_CHECK(stack_root.add_child(&stack_middle));
        VISOR_CHECK(stack_root.add_child(&stack_top));
        stack_root.set_dirty_sink(&stack_reports);
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{5, 5}), static_cast<Widget*>(&stack_top));

        // raise(): the middle child goes to the top of the list.
        VISOR_CHECK(stack_middle.raise());
        VISOR_CHECK_EQ(stack_root.child(0), static_cast<Widget*>(&stack_bottom));
        VISOR_CHECK_EQ(stack_root.child(1), static_cast<Widget*>(&stack_top));
        VISOR_CHECK_EQ(stack_root.child(2), static_cast<Widget*>(&stack_middle));
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{5, 5}), static_cast<Widget*>(&stack_middle));
        widget_order.clear();
        stack_root.draw(painter);
        const std::array<int, 4> raised_order{20, 21, 23, 22};
        VISOR_CHECK_EQ(widget_order.size(), raised_order.size());
        for (std::size_t i = 0; i < raised_order.size(); ++i) {
            VISOR_CHECK_EQ(widget_order[i], raised_order[i]);
        }
        VISOR_CHECK_EQ(stack_reports.count(), std::size_t{1});
        VISOR_CHECK_EQ(stack_reports.at(0), left_bounds);
        stack_reports.clear();
        // Already on top: no order change, no debt.
        VISOR_CHECK(!stack_middle.raise());
        VISOR_CHECK_EQ(stack_reports.count(), std::size_t{0});

        // lower(): the middle child goes to the bottom, so the top child is
        // asked first again.
        VISOR_CHECK(stack_middle.lower());
        VISOR_CHECK_EQ(stack_root.child(0), static_cast<Widget*>(&stack_middle));
        VISOR_CHECK_EQ(stack_root.child(1), static_cast<Widget*>(&stack_bottom));
        VISOR_CHECK_EQ(stack_root.child(2), static_cast<Widget*>(&stack_top));
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{5, 5}), static_cast<Widget*>(&stack_top));
        widget_order.clear();
        stack_root.draw(painter);
        const std::array<int, 4> lowered_order{20, 22, 21, 23};
        VISOR_CHECK_EQ(widget_order.size(), lowered_order.size());
        for (std::size_t i = 0; i < lowered_order.size(); ++i) {
            VISOR_CHECK_EQ(widget_order[i], lowered_order[i]);
        }
        VISOR_CHECK_EQ(stack_reports.count(), std::size_t{1});
        VISOR_CHECK_EQ(stack_reports.at(0), left_bounds);
        VISOR_CHECK(!stack_middle.lower());
        // A widget outside any parent has no order to change.
        Tracer stack_orphan(left_bounds, 24, widget_order);
        VISOR_CHECK(!stack_orphan.raise());
        VISOR_CHECK(!stack_orphan.lower());

        // Half-open boxes: the right and bottom edges belong to the parent,
        // not to the children that end there.
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{10, 5}), static_cast<Widget*>(&stack_root));
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{5, 10}), static_cast<Widget*>(&stack_root));
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{20, 5}), static_cast<Widget*>(nullptr));

        // A hidden top child neither paints nor hits; the next visible child
        // in the stack takes the point.
        stack_top.set_visible(false);
        VISOR_CHECK_EQ(stack_root.hit_test(PointI{5, 5}), static_cast<Widget*>(&stack_bottom));
        widget_order.clear();
        stack_root.draw(painter);
        const std::array<int, 3> hidden_order{20, 22, 21};
        VISOR_CHECK_EQ(widget_order.size(), hidden_order.size());
        for (std::size_t i = 0; i < hidden_order.size(); ++i) {
            VISOR_CHECK_EQ(widget_order[i], hidden_order[i]);
        }
        // A hidden widget can still be reordered, but it owns no pixels yet,
        // so the move reports nothing.
        stack_reports.clear();
        VISOR_CHECK(stack_top.lower());
        VISOR_CHECK_EQ(stack_root.child(0), static_cast<Widget*>(&stack_top));
        VISOR_CHECK_EQ(stack_reports.count(), std::size_t{0});
        stack_top.set_visible(true);
    }
}

void run_focus_domain_cases() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;

    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}};
    const RectI left_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}};

    // Focus domain: an explicit scope bounds the Tab walk to one window
    // subtree. Each window cycles internally without crossing, the root
    // domain still wraps across the whole tree, and hidden branches stay out.
    {
        visor::ui::Rectangle desktop(root_bounds);
        visor::ui::Rectangle window_a(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}});
        visor::ui::Rectangle window_b(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 10, .h = 10}});
        visor::ui::Rectangle bare_window(
            RectI{.pos = PointI{0, 10}, .size = SizeI{.w = 10, .h = 10}});
        visor::ui::Button a1(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 4, .h = 4}});
        visor::ui::Button a2(RectI{.pos = PointI{6, 0}, .size = SizeI{.w = 4, .h = 4}});
        visor::ui::Button b1(RectI{.pos = PointI{10, 0}, .size = SizeI{.w = 4, .h = 4}});
        visor::ui::Button b2(RectI{.pos = PointI{16, 0}, .size = SizeI{.w = 4, .h = 4}});
        VISOR_CHECK(desktop.add_child(&window_a));
        VISOR_CHECK(desktop.add_child(&window_b));
        VISOR_CHECK(desktop.add_child(&bare_window));
        VISOR_CHECK(window_a.add_child(&a1));
        VISOR_CHECK(window_a.add_child(&a2));
        VISOR_CHECK(window_b.add_child(&b1));
        VISOR_CHECK(window_b.add_child(&b2));

        // The default scope is the root domain: every focusable in draw order,
        // wrapping past the end.
        VISOR_CHECK(desktop.advance_focus());
        VISOR_CHECK(a1.focused());
        VISOR_CHECK(desktop.advance_focus());
        VISOR_CHECK(a2.focused());
        VISOR_CHECK(desktop.advance_focus());
        VISOR_CHECK(b1.focused());
        VISOR_CHECK(desktop.advance_focus());
        VISOR_CHECK(b2.focused());

        // Window A's own scope never crosses into Window B and wraps at its
        // own last focusable.
        VISOR_CHECK(desktop.advance_focus(&window_a));
        VISOR_CHECK(a1.focused());
        VISOR_CHECK(!b2.focused());
        VISOR_CHECK(desktop.advance_focus(&window_a));
        VISOR_CHECK(a2.focused());
        VISOR_CHECK(desktop.advance_focus(&window_a));
        VISOR_CHECK(a1.focused());

        // Switching scope to Window B starts at B's first focusable and drops
        // A's ring, keeping the one-holder invariant.
        VISOR_CHECK(desktop.advance_focus(&window_b));
        VISOR_CHECK(b1.focused());
        VISOR_CHECK(!a1.focused());
        VISOR_CHECK(desktop.advance_focus(&window_b));
        VISOR_CHECK(b2.focused());
        VISOR_CHECK(desktop.advance_focus(&window_b));
        VISOR_CHECK(b1.focused());

        // Back to the root domain: from B's first, the walk crosses to B's
        // second and then wraps all the way to A's first.
        VISOR_CHECK(desktop.advance_focus());
        VISOR_CHECK(b2.focused());
        VISOR_CHECK(desktop.advance_focus());
        VISOR_CHECK(a1.focused());

        // A domain with no visible focusable child answers false and moves
        // nobody, even when the current holder lives outside it.
        VISOR_CHECK(!desktop.advance_focus(&bare_window));
        VISOR_CHECK(a1.focused());

        // A hidden branch is skipped inside its own scope too: hiding A's
        // second button leaves A's first as the only stop.
        a2.set_visible(false);
        VISOR_CHECK(desktop.advance_focus(&window_a));
        VISOR_CHECK(a1.focused());
        // A hidden window domain has nothing to hand over, so focus stays in
        // the visible window.
        window_b.set_visible(false);
        VISOR_CHECK(!desktop.advance_focus(&window_b));
        VISOR_CHECK(a1.focused());

        // A pointer that is not a member of this tree is not followed; the
        // walk falls back to the root domain, which is how a torn-down
        // window's scope degrades.
        visor::ui::Rectangle stranger(left_bounds);
        window_b.set_visible(true);
        VISOR_CHECK(desktop.advance_focus(&stranger));
        VISOR_CHECK(b1.focused());
        VISOR_CHECK(!a1.focused());

        // Naming this widget itself is the root domain again.
        VISOR_CHECK(desktop.advance_focus(&desktop));
        VISOR_CHECK(b2.focused());
    }
}

void run_remove_child_cases() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::ui::Widget;

    const RectI root_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 20, .h = 20}};
    const RectI child_bounds{.pos = PointI{0, 0}, .size = SizeI{.w = 10, .h = 10}};
    const RecordingPainter painter;

    visor::ui::DirtyRegions reports;
    Tracer root(root_bounds, 40, widget_order);
    Tracer bottom(child_bounds, 41, widget_order);
    Tracer middle(child_bounds, 42, widget_order);
    Tracer top(child_bounds, 43, widget_order);
    VISOR_CHECK(root.add_child(&bottom));
    VISOR_CHECK(root.add_child(&middle));
    VISOR_CHECK(root.add_child(&top));
    root.set_dirty_sink(&reports);

    // Removing the middle child shifts the later sibling down, reports the
    // covered box, and clears the parent link so raise/lower have nothing left
    // to reorder.
    VISOR_CHECK(root.remove_child(&middle));
    VISOR_CHECK_EQ(root.child_count(), std::size_t{2});
    VISOR_CHECK_EQ(root.child(0), static_cast<Widget*>(&bottom));
    VISOR_CHECK_EQ(root.child(1), static_cast<Widget*>(&top));
    VISOR_CHECK_EQ(reports.count(), std::size_t{1});
    VISOR_CHECK_EQ(reports.at(0), child_bounds);
    VISOR_CHECK(!middle.raise());
    VISOR_CHECK(!middle.lower());

    // Both walks follow the shortened list.
    widget_order.clear();
    root.draw(painter);
    const std::array<int, 3> after_remove{40, 41, 43};
    VISOR_CHECK_EQ(widget_order.size(), after_remove.size());
    for (std::size_t i = 0; i < after_remove.size(); ++i) {
        VISOR_CHECK_EQ(widget_order[i], after_remove[i]);
    }
    VISOR_CHECK_EQ(root.hit_test(PointI{5, 5}), static_cast<Widget*>(&top));

    // Re-adoption is allowed after the link cleared, and the child joins on
    // top like any other late child.
    VISOR_CHECK(root.add_child(&middle));
    VISOR_CHECK_EQ(root.child_count(), std::size_t{3});
    VISOR_CHECK_EQ(root.child(2), static_cast<Widget*>(&middle));
    VISOR_CHECK_EQ(root.hit_test(PointI{5, 5}), static_cast<Widget*>(&middle));

    // A null child and a stranger are refused without touching the list.
    Tracer stranger(child_bounds, 44, widget_order);
    VISOR_CHECK(!root.remove_child(nullptr));
    VISOR_CHECK(!root.remove_child(&stranger));
    VISOR_CHECK_EQ(root.child_count(), std::size_t{3});

    // A second removal of the same child is refused: the first one already
    // cleared the parent link.
    VISOR_CHECK(root.remove_child(&middle));
    VISOR_CHECK(!root.remove_child(&middle));
    VISOR_CHECK_EQ(root.child_count(), std::size_t{2});

    // A hidden child painted nothing, so leaving owes no repaint.
    top.set_visible(false);
    reports.clear();
    VISOR_CHECK(root.remove_child(&top));
    VISOR_CHECK_EQ(reports.count(), std::size_t{0});

    // The detached subtree no longer reports into the old collector.
    reports.clear();
    middle.set_bounds(RectI{.pos = PointI{2, 2}, .size = SizeI{.w = 4, .h = 4}});
    VISOR_CHECK_EQ(reports.count(), std::size_t{0});

    // A focused widget inside the removed subtree loses focus, so re-adoption
    // cannot bring back a ring nobody is holding.
    visor::ui::Rectangle desktop(root_bounds);
    visor::ui::Rectangle window(root_bounds);
    visor::ui::Button field(child_bounds);
    VISOR_CHECK(window.add_child(&field));
    VISOR_CHECK(desktop.add_child(&window));
    field.set_focused(true);
    VISOR_CHECK_EQ(desktop.focused_widget(), static_cast<Widget*>(&field));
    VISOR_CHECK(desktop.remove_child(&window));
    VISOR_CHECK_EQ(desktop.focused_widget(), static_cast<Widget*>(nullptr));
    VISOR_CHECK(!field.focused());
    VISOR_CHECK(desktop.add_child(&window));
    VISOR_CHECK(!field.focused());
}

} // namespace

void run_widget_tests() {
    using visor::base::PointI;
    using visor::base::RectI;
    using visor::base::SizeI;
    using visor::canvas::ColorRole;
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

    run_z_order_cases();
    run_remove_child_cases();

    // The root's own switch turns off the whole tree at once.
    widget_order.clear();
    root.set_visible(false);
    root.draw(painter);
    VISOR_CHECK(widget_order.empty());
    VISOR_CHECK_EQ(root.hit_test((PointI{5, 5})), static_cast<Widget*>(nullptr));
    root.set_visible(true);

    // The child list has no capacity bound: a dozen siblings are all adopted,
    // child() walks them in add order, and a middle removal relinks the rest
    // without disturbing that order.
    Tracer hoard(root_bounds, 9, widget_order);
    std::array<Tracer, 12> slots = {
        Tracer(root_bounds, 100, widget_order), Tracer(root_bounds, 101, widget_order),
        Tracer(root_bounds, 102, widget_order), Tracer(root_bounds, 103, widget_order),
        Tracer(root_bounds, 104, widget_order), Tracer(root_bounds, 105, widget_order),
        Tracer(root_bounds, 106, widget_order), Tracer(root_bounds, 107, widget_order),
        Tracer(root_bounds, 108, widget_order), Tracer(root_bounds, 109, widget_order),
        Tracer(root_bounds, 110, widget_order), Tracer(root_bounds, 111, widget_order),
    };
    constexpr std::size_t slot_count = std::size(slots);
    for (Tracer& slot : slots) {
        VISOR_CHECK(hoard.add_child(&slot));
    }
    VISOR_CHECK_EQ(hoard.child_count(), slot_count);
    VISOR_CHECK_EQ(hoard.child(0), static_cast<Widget*>(slots.data()));
    VISOR_CHECK_EQ(hoard.child(7), static_cast<Widget*>(&slots[7]));
    VISOR_CHECK_EQ(hoard.child(slot_count - 1), static_cast<Widget*>(&slots[slot_count - 1]));
    VISOR_CHECK_EQ(hoard.child(slot_count), static_cast<Widget*>(nullptr));

    VISOR_CHECK(hoard.remove_child(&slots[6]));
    VISOR_CHECK_EQ(hoard.child_count(), slot_count - 1);
    VISOR_CHECK_EQ(hoard.child(5), static_cast<Widget*>(&slots[5]));
    VISOR_CHECK_EQ(hoard.child(6), static_cast<Widget*>(&slots[7]));
    VISOR_CHECK_EQ(hoard.child(slot_count - 2), static_cast<Widget*>(&slots[slot_count - 1]));

    // raise() and lower() walk the same long list, and their edge checks hold.
    VISOR_CHECK(slots[0].raise());
    VISOR_CHECK_EQ(hoard.child(slot_count - 2), static_cast<Widget*>(slots.data()));
    VISOR_CHECK(slots[0].lower());
    VISOR_CHECK_EQ(hoard.child(0), static_cast<Widget*>(slots.data()));

    // A cycle is refused: no widget may become its own ancestor, and the
    // walk-based checks leave the list untouched.
    VISOR_CHECK(!hoard.add_child(&hoard));
    VISOR_CHECK(!slots[0].add_child(&hoard));
    VISOR_CHECK_EQ(hoard.child_count(), slot_count - 1);
    // A widget already owned by a parent cannot be adopted twice: the old
    // parent's order would silently disagree with the new one.
    VISOR_CHECK(!root.add_child(&left));
    VISOR_CHECK_EQ(root.child_count(), std::size_t{2});

    // Rectangle paints exactly its own bounds with its ink, nothing else.
    visor::ui::Rectangle rect(left_bounds);
    rect.fill = ColorRole::Background;
    rect.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 1U);
    VISOR_CHECK_EQ(painter.fills()[0].area, left_bounds);
    VISOR_CHECK_EQ(painter.fills()[0].ink, ColorRole::Background);

    // Button: the state flip and the ink flip agree on both edges of a press.
    visor::ui::Button button(left_bounds);
    VISOR_CHECK(!button.pressed());
    button.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 2U);
    VISOR_CHECK_EQ(painter.fills()[1].ink, ColorRole::Accent);
    button.on_pointer_down(PointI{5, 5}, {});
    VISOR_CHECK(button.pressed());
    button.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 4U);
    VISOR_CHECK_EQ(painter.fills()[2].ink, ColorRole::Border);
    VISOR_CHECK_EQ(painter.fills()[3].ink, ColorRole::Background);
    button.on_pointer_up(PointI{5, 5});
    VISOR_CHECK(!button.pressed());
    button.draw(painter);
    VISOR_CHECK_EQ(painter.fills().size(), 5U);
    VISOR_CHECK_EQ(painter.fills()[4].ink, ColorRole::Accent);

    // The same story on real pixels: unpressed lights its bounds, pressed
    // clears them again.
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> frame;
    const visor::ui::CanvasPainter frame_painter{frame};
    frame.set_clear(false);
    button.draw(frame_painter);
    VISOR_CHECK(frame.get(PointI{4, 4}) == true);
    button.on_pointer_down(PointI{5, 5}, {});
    frame.set_clear(false);
    button.draw(frame_painter);
    VISOR_CHECK(frame.get(PointI{4, 4}) == false);

    // Replay contract: a panel with a pressed button on top, drawn twice,
    // must leave identical bytes -- draw reads state and touches nothing.
    visor::ui::Rectangle panel(root_bounds);
    visor::ui::Button redial(left_bounds);
    VISOR_CHECK(panel.add_child(&redial));
    redial.on_pointer_down(PointI{5, 5}, {});
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> replay_first;
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> replay_second;
    replay_first.set_clear(false);
    replay_second.set_clear(false);
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

    run_focus_domain_cases();

    // The focus ring on real pixels: the holder's border turns dark, its
    // interior stays lit, and dropping the focus restores the plain fill.
    visor::canvas::Canvas<visor::base::SizeI{.w = 20, .h = 20}> ring_frame;
    const visor::ui::CanvasPainter ring_painter{ring_frame};
    ring_frame.set_clear(false);
    alpha.set_focused(true);
    alpha.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == false);
    VISOR_CHECK(ring_frame.get(PointI{2, 2}) == true);
    alpha.set_focused(false);
    ring_frame.set_clear(false);
    alpha.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == true);

    // A checked checkbox has a visible cut-out check; unchecked is an empty outline.
    visor::ui::Checkbox toggle(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 14, .h = 14}});
    VISOR_CHECK(!toggle.checked());
    toggle.on_pointer_down(PointI{5, 5}, {});
    VISOR_CHECK(toggle.checked());
    ring_frame.set_clear(false);
    toggle.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{3, 2}) == true);  // outline
    VISOR_CHECK(ring_frame.get(PointI{4, 4}) == true);  // filled face
    VISOR_CHECK(ring_frame.get(PointI{3, 7}) == false); // check starts here
    toggle.on_pointer_down(PointI{5, 5}, {});
    ring_frame.set_clear(false);
    toggle.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{3, 2}) == true);
    VISOR_CHECK(ring_frame.get(PointI{4, 4}) == false); // empty face
    toggle.on_pointer_down(PointI{5, 5}, {});
    toggle.set_focused(true);
    ring_frame.set_clear(false);
    toggle.draw(ring_painter);
    VISOR_CHECK(ring_frame.get(PointI{0, 0}) == false);
    VISOR_CHECK(ring_frame.get(PointI{1, 0}) == true);  // dotted focus
    VISOR_CHECK(ring_frame.get(PointI{1, 1}) == false); // gap
    VISOR_CHECK(ring_frame.get(PointI{3, 2}) == true);  // outline
    VISOR_CHECK(ring_frame.get(PointI{4, 4}) == true);  // checked face

    // The replay promise now covers focus and a toggled control too: a panel
    // with a pressed button and a checked, focused checkbox, drawn twice,
    // still leaves identical bytes.
    visor::ui::Checkbox replay_toggle(left_bounds);
    VISOR_CHECK(panel.add_child(&replay_toggle));
    replay_toggle.on_pointer_down(PointI{5, 5}, {});
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
    wired_one.on_pointer_down(PointI{5, 5}, {}); // a control reports its own bounds
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
    late.on_pointer_down(PointI{5, 5}, {});
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
