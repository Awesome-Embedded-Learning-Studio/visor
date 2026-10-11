#include <array>
#include <cstddef>
#include <vector>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/widget.hpp"

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

    void fill(visor::base::RectI area, visor::canvas::Color ink) const noexcept override {
        fills_.push_back(Fill{.area = area, .ink = ink});
    }
    void plot(visor::base::PointI p, visor::canvas::Color ink) const noexcept override {
        plots_.push_back(Plotted{.at = p, .ink = ink});
    }

    [[nodiscard]] const std::vector<Fill>& fills() const noexcept { return fills_; }
    [[nodiscard]] const std::vector<Plotted>& plots() const noexcept { return plots_; }

  private:
    mutable std::vector<Fill> fills_;
    mutable std::vector<Plotted> plots_;
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
}
