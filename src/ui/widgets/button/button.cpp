#include "visor/ui/widgets/button/button.hpp"

#include <cstdint>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/control_style.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/text.hpp"
#include "visor/ui/theme.hpp"

namespace visor::ui {

void Button::set_on_activate(ActivateHandler handler, void* context) noexcept {
    on_activate_ = handler;
    activate_context_ = context;
}

void Button::set_label(std::string_view text, const draw::MonoFont& font) noexcept {
    if (label_ == text && font_ == &font) {
        return;
    }
    label_ = text;
    font_ = &font;
    invalidate(bounds());
}

void Button::set_outlined(bool on) noexcept {
    if (outlined_ == on) {
        return;
    }
    outlined_ = on;
    invalidate(bounds());
}

bool Button::pressed() const noexcept {
    return pointer_pressed_ || key_pressed_;
}

void Button::on_pointer_down([[maybe_unused]] base::PointI at,
                             [[maybe_unused]] host::Modifiers modifiers) noexcept {
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

void Button::on_key_down(std::uint32_t code, [[maybe_unused]] host::Modifiers modifiers) noexcept {
    // Enter and Space are the keyboard's press, the codes events.hpp names.
    // Space also arrives as its code point (kCharSpace, 0x20) when a text
    // host types it -- the same press under either number. A repeat of the
    // held key -- or a second key while one is already held -- changes
    // nothing: the first press owns the slot until its own up.
    if (code != host::kKeyEnter && code != host::kKeySpace && code != host::kCharSpace) {
        return;
    }
    if (key_pressed_) {
        return;
    }
    key_pressed_ = true;
    held_key_ = code;
    invalidate(bounds());
}

void Button::on_key_up(std::uint32_t code, [[maybe_unused]] host::Modifiers modifiers) noexcept {
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
    // A filled face takes the accent pair, a hollow one the ground and its
    // border; the corner radius travels with the scheme.
    const CornerRadius corners = theme() != nullptr ? theme()->corner_radius : CornerRadius{};
    // Keep the silhouette while pressed so an empty button never disappears.
    const bool solid = outlined_ == pressed();
    const auto edge = solid ? canvas::ColorRole::Accent : canvas::ColorRole::Border;
    const auto face = solid ? canvas::ColorRole::Accent : canvas::ColorRole::Background;
    const auto text = solid ? canvas::ColorRole::OnAccent : canvas::ColorRole::Foreground;
    painter.fill_rounded(bounds(), edge, corners);
    if (!solid) {
        painter.fill_rounded(base::inset(bounds(), 1), face, inset_corners(corners));
    }
    if (font_ != nullptr) {
        const base::SizeI size = draw::measure_text(*font_, label_, 1);
        const base::PointI at{bounds().pos.x + ((bounds().size.w - size.w) / 2),
                              bounds().pos.y + ((bounds().size.h - size.h) / 2)};
        draw_text_clipped(painter, base::inset(bounds(), 2), at, *font_, label_, text, 1);
    }
}

void Button::activate() noexcept {
    if (on_activate_ != nullptr) {
        on_activate_(*this, activate_context_);
    }
}

} // namespace visor::ui
