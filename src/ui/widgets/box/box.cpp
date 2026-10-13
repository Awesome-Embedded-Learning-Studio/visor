#include "visor/ui/widgets/box/box.hpp"

#include <cstddef>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace visor::ui {

HBox::HBox(base::RectI bounds) noexcept : Widget{bounds} {}

void HBox::set_spacing(int spacing) noexcept {
    spacing_ = spacing;
}

void HBox::layout() noexcept {
    int cursor = bounds().pos.x;
    for (std::size_t i = 0; i < child_count(); ++i) {
        Widget* widget = child(i);
        const int width = widget->bounds().size.w;
        // Keep the main extent, take the cross extent from the box: a row of
        // controls shares one height, the row does not guess at theirs.
        widget->set_bounds(base::RectI{.pos = base::PointI{cursor, bounds().pos.y},
                                       .size = base::SizeI{.w = width, .h = bounds().size.h}});
        cursor += width + spacing_;
    }
}

VBox::VBox(base::RectI bounds) noexcept : Widget{bounds} {}

void VBox::set_spacing(int spacing) noexcept {
    spacing_ = spacing;
}

void VBox::layout() noexcept {
    int cursor = bounds().pos.y;
    for (std::size_t i = 0; i < child_count(); ++i) {
        Widget* widget = child(i);
        const int height = widget->bounds().size.h;
        widget->set_bounds(base::RectI{.pos = base::PointI{bounds().pos.x, cursor},
                                       .size = base::SizeI{.w = bounds().size.w, .h = height}});
        cursor += height + spacing_;
    }
}

} // namespace visor::ui
