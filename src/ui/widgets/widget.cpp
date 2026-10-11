#include <cstddef>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/widgets/widget.hpp"

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
// depth constant the spec never asked for.
// NOLINTNEXTLINE(misc-no-recursion)
void Widget::draw(const Painter& painter) const noexcept {
    if (!visible_) {
        return;
    }
    on_draw(painter);
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

} // namespace visor::ui
