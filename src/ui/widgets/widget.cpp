#include <cstddef>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

// Focus feedback is framework policy, not a control's choice: draw() paints
// this one-pixel border after on_draw() so every focusable control shows the
// same ring and none of them can forget it. Controls keep their content one
// pixel inside the bounds, which leaves the ring a lit margin to sit on.
void paint_focus_border(const visor::ui::Painter& painter, const RectI& bounds) noexcept {
    const PointI at = bounds.pos;
    const int w = bounds.size.w;
    const int h = bounds.size.h;
    painter.fill(RectI{.pos = at, .size = SizeI{.w = w, .h = 1}}, visor::canvas::Color::Off);
    painter.fill(RectI{.pos = PointI{at.x, at.y + h - 1}, .size = SizeI{.w = w, .h = 1}},
                 visor::canvas::Color::Off);
    painter.fill(RectI{.pos = at, .size = SizeI{.w = 1, .h = h}}, visor::canvas::Color::Off);
    painter.fill(RectI{.pos = PointI{at.x + w - 1, at.y}, .size = SizeI{.w = 1, .h = h}},
                 visor::canvas::Color::Off);
}

} // namespace

namespace visor::ui {

Widget::Widget(base::RectI bounds) noexcept : bounds_{bounds} {}

const base::RectI& Widget::bounds() const noexcept { return bounds_; }

bool Widget::visible() const noexcept { return visible_; }

void Widget::set_visible(bool on) noexcept { visible_ = on; }

bool Widget::add_child(Widget* child) noexcept {
    if (child == nullptr || child_count_ == kMaxChildren) {
        return false;
    }
    children_[child_count_] = child;
    ++child_count_;
    return true;
}

std::size_t Widget::child_count() const noexcept { return child_count_; }

// The recursive walk is the tree's honest shape: a real scene nests only a few
// levels deep (a screen, a panel, a control), and an explicit stack would buy a
// depth constant the spec never asked for. The focus walks further down share
// this reasoning.
// NOLINTNEXTLINE(misc-no-recursion)
void Widget::draw(const Painter& painter) const noexcept {
    if (!visible_) {
        return;
    }
    on_draw(painter);
    if (focused_) {
        paint_focus_border(painter, bounds_);
    }
    for (std::size_t i = 0; i < child_count_; ++i) {
        children_[i]->draw(painter);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
Widget* Widget::hit_test(base::PointI at) noexcept {
    if (!visible_ || !bounds_.contains(at)) {
        return nullptr;
    }
    // Reverse walk: the child added last paints on top of its siblings, so it is
    // asked first. A child that accepts the point ends the search; a child that
    // declines hands the point back to the remaining siblings and finally here.
    for (std::size_t i = child_count_; i > 0; --i) {
        if (Widget* hit = children_[i - 1]->hit_test(at); hit != nullptr) {
            return hit;
        }
    }
    return this;
}

bool Widget::focused() const noexcept { return focused_; }

void Widget::set_focused(bool on) noexcept { focused_ = on; }

// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
Widget* Widget::find_focused_in(Widget& node) noexcept {
    if (node.focused_) {
        return &node;
    }
    for (std::size_t i = 0; i < node.child_count_; ++i) {
        if (Widget* found = find_focused_in(*node.children_[i]); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
Widget* Widget::find_focusable_after(Widget& node, Widget* current, bool& seen_current) noexcept {
    if (!node.visible_) {
        return nullptr; // A hidden branch takes no focus, matching draw() and hit_test().
    }
    if (node.focusable() && seen_current) {
        return &node;
    }
    if (&node == current) {
        seen_current = true;
    }
    for (std::size_t i = 0; i < node.child_count_; ++i) {
        if (Widget* next = find_focusable_after(*node.children_[i], current, seen_current);
            next != nullptr) {
            return next;
        }
    }
    return nullptr;
}

bool Widget::advance_focus() noexcept {
    Widget* current = find_focused_in(*this);
    bool seen_current = (current == nullptr);
    Widget* target = find_focusable_after(*this, current, seen_current);
    if (target == nullptr && current != nullptr) {
        // Wrap: nothing focusable after the holder, restart from the top. The
        // restart may land back on the holder itself, which is the tree saying
        // it has exactly one focusable widget, and focus stays there.
        seen_current = true;
        target = find_focusable_after(*this, nullptr, seen_current);
    }
    if (target == nullptr) {
        return false;
    }
    if (current != nullptr && current != target) {
        current->set_focused(false);
    }
    target->set_focused(true);
    return true;
}

} // namespace visor::ui
