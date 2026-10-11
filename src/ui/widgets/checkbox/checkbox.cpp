#include "visor/ui/widgets/checkbox/checkbox.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

bool Checkbox::checked() const noexcept {
    return checked_;
}

void Checkbox::on_pointer_down() noexcept {
    checked_ = !checked_;
}

void Checkbox::on_draw(const Painter& painter) const noexcept {
    // The box rides two pixels inside the bounds: one row for the focus ring
    // the tree paints after this, one lit margin between ring and box so the
    // two dark lines never merge into one thick border. A box smaller than the
    // insets turns empty, and the Pen's fill drops empty rectangles silently.
    const base::RectI box{.pos = base::PointI{bounds().pos.x + 2, bounds().pos.y + 2},
                          .size = base::SizeI{.w = bounds().size.w - 4, .h = bounds().size.h - 4}};
    painter.fill(box, canvas::Color::Off);
    if (!checked_) {
        const base::RectI inner{
            .pos = base::PointI{box.pos.x + 1, box.pos.y + 1},
            .size = base::SizeI{.w = box.size.w - 2, .h = box.size.h - 2}};
        painter.fill(inner, canvas::Color::On);
    }
}

} // namespace visor::ui
