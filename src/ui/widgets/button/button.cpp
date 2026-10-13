#include "visor/ui/widgets/button/button.hpp"

#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

void Button::set_on_activate(ActivateHandler handler, void* context) noexcept {
    on_activate_ = handler;
    activate_context_ = context;
}

bool Button::pressed() const noexcept {
    return pointer_pressed_ || key_pressed_;
}

void Button::on_pointer_down([[maybe_unused]] base::PointI at) noexcept {
    pointer_pressed_ = true;
    invalidate(bounds());
}

void Button::on_pointer_up(base::PointI at) noexcept {
    // Capture routes the release here wherever it landed; the click
    // synthesis is the judgment of where. A release back inside the bounds
    // completes the press as an activation, a release outside cancels it --
    // the visual goes either way.
    pointer_pressed_ = false;
    invalidate(bounds());
    if (bounds().contains(at)) {
        activate();
    }
}

void Button::on_key_down(std::uint32_t code) noexcept {
    // Enter and Space are the keyboard's press, the codes events.hpp names.
    // A repeat of the held key -- or a second key while one is already held
    // -- changes nothing: the first press owns the slot until its own up.
    if (code != host::kKeyEnter && code != host::kKeySpace) {
        return;
    }
    if (key_pressed_) {
        return;
    }
    key_pressed_ = true;
    held_key_ = code;
    invalidate(bounds());
}

void Button::on_key_up(std::uint32_t code) noexcept {
    // Only the up that matches the held key completes the activation; every
    // other release is somebody else's press coming home.
    if (!key_pressed_ || code != held_key_) {
        return;
    }
    key_pressed_ = false;
    invalidate(bounds());
    activate();
}

void Button::on_draw(const Painter& painter) const noexcept {
    painter.fill(bounds(), pressed() ? canvas::Color::Off : canvas::Color::On);
}

void Button::activate() noexcept {
    if (on_activate_ != nullptr) {
        on_activate_(*this, activate_context_);
    }
}

} // namespace visor::ui
