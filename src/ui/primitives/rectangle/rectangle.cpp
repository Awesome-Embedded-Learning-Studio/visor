#include "visor/ui/primitives/rectangle/rectangle.hpp"

#include "visor/ui/painter.hpp"

namespace visor::ui {

void Rectangle::on_draw(const Painter& painter) const noexcept {
    painter.fill(bounds(), fill);
}

} // namespace visor::ui
