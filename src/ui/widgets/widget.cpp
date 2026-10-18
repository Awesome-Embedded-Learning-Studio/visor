#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/identity/id_session.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace {

using visor::base::PointI;
using visor::base::RectI;
using visor::canvas::ColorRole;

// One process-wide identity space for every widget. The class owns the atomic
// and the zero-reservation rule; another subsystem can start its own session.
constinit visor::base::IdSession<std::uint32_t> widget_ids;

// Alternating ink and ground stays visible on both filled and hollow controls.
void paint_focus_border(const visor::ui::Painter& painter, const RectI& bounds) noexcept {
    const auto fg = ColorRole::Foreground;
    const auto bg = ColorRole::Background;
    if (bounds.is_empty()) {
        return;
    }
    for (int x = 0; x < bounds.size.w; ++x) {
        const auto ink = x % 2 == 0 ? bg : fg;
        painter.plot(PointI{bounds.pos.x + x, bounds.pos.y}, ink);
        painter.plot(PointI{bounds.pos.x + x, bounds.pos.y + bounds.size.h - 1}, ink);
    }
    for (int y = 0; y < bounds.size.h; ++y) {
        const auto ink = y % 2 == 0 ? bg : fg;
        painter.plot(PointI{bounds.pos.x, bounds.pos.y + y}, ink);
        painter.plot(PointI{bounds.pos.x + bounds.size.w - 1, bounds.pos.y + y}, ink);
    }
}

} // namespace

namespace visor::ui {

Widget::Widget(base::RectI bounds) noexcept : bounds_{bounds}, id_{widget_ids.next()} {}

const base::RectI& Widget::bounds() const noexcept {
    return bounds_;
}

bool Widget::visible() const noexcept {
    return visible_;
}

void Widget::set_bounds(base::RectI bounds) noexcept {
    if (bounds == bounds_) {
        return; // Same box, same pixels, no debt.
    }
    // Both boxes owe repaint: the old one holds pixels the widget no longer
    // claims (what is underneath shows through), the new one claims pixels
    // it never painted. A named local makes each report's box visible where
    // the report is made, instead of two identical reads of the member
    // whose meaning depends on which side of the assignment they sit.
    const base::RectI old_bounds = bounds_;
    bounds_ = bounds;
    invalidate(old_bounds);
    invalidate(bounds_);
}

void Widget::set_visible(bool on) noexcept {
    if (visible_ == on) {
        return; // No flip, no pixel change, no debt.
    }
    visible_ = on;
    // Hiding owes the old pixels (what is behind shows through), showing owes
    // the new ones; the area is bounds either way.
    invalidate(bounds_);
}

// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
void Widget::set_dirty_sink(DirtyRegions* sink) noexcept {
    dirty_ = sink;
    for (std::size_t i = 0; i < child_count_; ++i) {
        children_[i]->set_dirty_sink(sink);
    }
}

// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
void Widget::set_theme(const Theme* theme) noexcept {
    if (theme_ == theme) {
        return; // same scheme wired twice wires nothing twice
    }
    theme_ = theme;
    // A switched scheme flips pixels everywhere a control consults it, so
    // every widget reports its own bounds -- runtime switching is this call.
    invalidate(bounds_);
    for (std::size_t i = 0; i < child_count_; ++i) {
        children_[i]->set_theme(theme);
    }
}

bool Widget::add_child(Widget* child) noexcept {
    if (child == nullptr || child_count_ == kMaxChildren || child->parent_ != nullptr) {
        return false;
    }
    children_[child_count_] = child;
    ++child_count_;
    child->parent_ = this;
    // The adopted subtree reports into the same sink and paints under the
    // same scheme, whenever either is wired.
    child->set_dirty_sink(dirty_);
    child->set_theme(theme_);
    return true;
}

bool Widget::remove_child(Widget* child) noexcept {
    if (child == nullptr || child->parent_ != this) {
        return false;
    }
    std::size_t index = 0;
    while (index < child_count_ && children_[index] != child) {
        ++index;
    }
    if (index == child_count_) {
        return false; // A parent link without a slot owns nothing to remove.
    }
    for (std::size_t i = index; i + 1 < child_count_; ++i) {
        children_[i] = children_[i + 1];
    }
    --child_count_;
    children_[child_count_] = nullptr;
    child->parent_ = nullptr;
    // The pixels the child covered now belong to whatever is beneath it. A
    // hidden child painted nothing, so it owes nothing on the way out.
    if (child->visible_) {
        invalidate(child->bounds());
    }
    // A detached subtree cannot hold this tree's focus. Releasing the ring
    // before the sink is unwired also catches a holder that drew outside the
    // child's own box.
    release_focus_in(*child);
    // The subtree must stop reporting into a collector it is no longer part
    // of; adopting it again wires the sink the same way add_child does.
    child->set_dirty_sink(nullptr);
    return true;
}

std::size_t Widget::child_count() const noexcept {
    return child_count_;
}

Widget* Widget::child(std::size_t index) noexcept {
    if (index >= child_count_) {
        return nullptr;
    }
    return children_[index];
}

bool Widget::raise() noexcept {
    return move_to_edge(true);
}

bool Widget::lower() noexcept {
    return move_to_edge(false);
}

bool Widget::move_to_edge(bool to_top) noexcept {
    if (parent_ == nullptr) {
        return false;
    }
    Widget* parent = parent_;
    std::size_t index = 0;
    while (index < parent->child_count_ && parent->children_[index] != this) {
        ++index;
    }
    if (index == parent->child_count_) {
        return false; // A stale parent link owns no order to change.
    }
    const std::size_t last = parent->child_count_ - 1;
    if (to_top) {
        if (index == last) {
            return false;
        }
        for (std::size_t i = index; i < last; ++i) {
            parent->children_[i] = parent->children_[i + 1];
        }
        parent->children_[last] = this;
    } else {
        if (index == 0) {
            return false;
        }
        for (std::size_t i = index; i > 0; --i) {
            parent->children_[i] = parent->children_[i - 1];
        }
        parent->children_[0] = this;
    }
    // Only this box changes owner. The overlap the widget now covers (raised)
    // or uncovers (lowered) lies inside it, and every pixel outside it keeps
    // the same topmost painter, so one report is the whole debt. A hidden
    // widget owns no pixels; showing it later reports the box again.
    if (visible_) {
        invalidate(bounds_);
    }
    return true;
}

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

bool Widget::focused() const noexcept {
    return focused_;
}

void Widget::set_focused(bool on) noexcept {
    if (focused_ == on) {
        return; // The ring is already in the pixels it should be.
    }
    focused_ = on;
    // The ring is framework policy painted at the holder's bounds, so the
    // framework reports that area too: focus moving is a repaint debt twice.
    invalidate(bounds_);
}

Widget* Widget::focused_widget() noexcept {
    return find_focused_in(*this);
}

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

// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
bool Widget::holds_widget(const Widget& node, const Widget* target) noexcept {
    if (target == nullptr) {
        return false;
    }
    if (&node == target) {
        return true;
    }
    for (std::size_t i = 0; i < node.child_count_; ++i) {
        if (holds_widget(*node.children_[i], target)) {
            return true;
        }
    }
    return false;
}

// NOLINTNEXTLINE(misc-no-recursion) -- same shape and same reason as draw().
void Widget::release_focus_in(Widget& node) noexcept {
    if (node.focused_) {
        node.set_focused(false);
    }
    for (std::size_t i = 0; i < node.child_count_; ++i) {
        release_focus_in(*node.children_[i]);
    }
}

bool Widget::advance_focus(Widget* scope) noexcept {
    // A scope that is not a live member of this subtree is treated as the
    // root domain. The membership walk only compares the pointer against live
    // nodes, so a scope left behind by a torn-down window is never followed.
    Widget* domain = (scope != nullptr && holds_widget(*this, scope)) ? scope : this;
    Widget* holder = find_focused_in(*this);
    Widget* current = find_focused_in(*domain);
    bool seen_current = (current == nullptr);
    Widget* target = find_focusable_after(*domain, current, seen_current);
    if (target == nullptr && current != nullptr) {
        // Wrap: nothing focusable after the holder, restart from the top of
        // the domain. The restart may land back on the holder itself, which is
        // the domain saying it has exactly one focusable widget.
        seen_current = true;
        target = find_focusable_after(*domain, nullptr, seen_current);
    }
    if (target == nullptr) {
        // An empty domain moves nobody: a holder outside it keeps its focus.
        return false;
    }
    // At most one widget holds focus per tree. A holder outside the domain is
    // still cleared, otherwise switching windows would leave two rings lit.
    if (holder != nullptr && holder != target) {
        holder->set_focused(false);
    }
    target->set_focused(true);
    return true;
}

} // namespace visor::ui
