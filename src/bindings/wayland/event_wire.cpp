#include "visor/bindings/wayland/event_wire.hpp"

#include <cstdint>
#include <optional>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace visor::host::wayland {
namespace {

// The arrows take the core's out-of-band values, the same relocation B16
// settled for the evdev and X11 translators: evdev's 103 to 108 sit inside
// printable ASCII, colliding head-on with a text host's code points.
constexpr std::uint32_t kEvdevArrowUp = 103;
constexpr std::uint32_t kEvdevArrowLeft = 105;
constexpr std::uint32_t kEvdevArrowRight = 106;
constexpr std::uint32_t kEvdevArrowDown = 108;

std::optional<std::uint32_t> named_key(std::uint32_t code) noexcept {
    switch (code) {
        case kEvdevArrowUp:
            return host::kKeyUp;
        case kEvdevArrowDown:
            return host::kKeyDown;
        case kEvdevArrowLeft:
            return host::kKeyLeft;
        case kEvdevArrowRight:
            return host::kKeyRight;
        default:
            return std::nullopt;
    }
}

std::optional<std::uint32_t> button_number(std::uint32_t button) noexcept {
    switch (button) {
        case kButtonLeft:
            return 1;
        case kButtonRight:
            return 2;
        case kButtonMiddle:
            return 3;
        default:
            return std::nullopt;
    }
}

} // namespace

host::Modifiers translate_modifiers(std::uint32_t xkb_mask) noexcept {
    return host::Modifiers{
        .shift = (xkb_mask & kModShift) != 0,
        .ctrl = (xkb_mask & kModControl) != 0,
        .alt = (xkb_mask & kModAlt) != 0,
        .super = (xkb_mask & kModSuper) != 0,
    };
}

std::optional<host::Event> translate_key(std::uint32_t code, bool pressed,
                                         host::Modifiers modifiers) noexcept {
    // No refusal branch exists on purpose: the evdev code space is small and
    // the core treats every unnamed code as opaque, so an unknown key is a
    // word worth carrying, not noise to drop.
    const std::uint32_t named = named_key(code).value_or(code);
    if (pressed) {
        return host::Event{host::KeyDown{.code = named, .modifiers = modifiers}};
    }
    return host::Event{host::KeyUp{.code = named, .modifiers = modifiers}};
}

std::optional<host::Event> translate_button(std::uint32_t button, bool pressed, base::PointI pos,
                                            host::Modifiers modifiers) noexcept {
    const auto number = button_number(button);
    if (!number.has_value()) {
        return std::nullopt;
    }
    if (pressed) {
        return host::Event{
            host::PointerDown{.button = *number, .pos = pos, .modifiers = modifiers}};
    }
    return host::Event{host::PointerUp{.button = *number, .pos = pos}};
}

host::Event translate_motion(base::PointI pos) noexcept {
    return host::Event{host::PointerMove{.pos = pos}};
}

std::optional<host::Event> translate_wheel(int horizontal_steps, int vertical_steps,
                                           base::PointI pos, host::Modifiers modifiers) noexcept {
    if (horizontal_steps == 0 && vertical_steps == 0) {
        return std::nullopt;
    }
    return host::Event{host::PointerWheel{
        .dx = horizontal_steps,
        .dy = vertical_steps,
        .pos = pos,
        .modifiers = modifiers,
    }};
}

} // namespace visor::host::wayland
