#include "visor/ui/widgets/button/button.hpp"

#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

bool Button::pressed() const noexcept {
    return pressed_;
}

void Button::on_pointer_down() noexcept {
    pressed_ = true;
}

void Button::on_pointer_up() noexcept {
    pressed_ = false;
}

void Button::on_draw(const Painter& painter) const noexcept {
    painter.fill(bounds(), pressed_ ? canvas::Color::Off : canvas::Color::On);
}

} // namespace visor::ui
