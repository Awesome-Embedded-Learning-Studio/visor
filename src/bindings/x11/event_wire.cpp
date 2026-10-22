#include "visor/bindings/x11/event_wire.hpp"

#include <cstdint>
#include <optional>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace visor::host::x11 {
namespace {

// The keysyms the core names map onto their named codes. Backspace, Tab and
// Enter keep the evdev numbers the core uses; the arrows take the out-of-band
// values because the keysym numbers for them (0xff52 band) are not what the
// core compares -- this table is the bridge between the two encodings.
std::optional<std::uint32_t> named_key_for(std::uint32_t keysym) noexcept {
    switch (keysym) {
        case XK_BackSpace:
            return kKeyBackspace;
        case XK_Tab:
            return kKeyTab;
        case XK_Return:
            return kKeyEnter;
        case XK_Up:
            return kKeyUp;
        case XK_Down:
            return kKeyDown;
        case XK_Left:
            return kKeyLeft;
        case XK_Right:
            return kKeyRight;
        default:
            return std::nullopt;
    }
}

host::Modifiers modifiers_from_state(unsigned int state) noexcept {
    return host::Modifiers{
        .shift = (state & ShiftMask) != 0,
        .ctrl = (state & ControlMask) != 0,
        .alt = (state & Mod1Mask) != 0,
        .super = (state & Mod4Mask) != 0,
    };
}

// Xlib names Button1 through Button5 only. Horizontal wheel buttons 6 and 7
// are still part of the X core button band, so the translator names them
// locally rather than inventing Xlib symbols.
constexpr unsigned int kWheelLeft = 6;
constexpr unsigned int kWheelRight = 7;

} // namespace

std::optional<host::Event> translate_key(std::uint32_t keysym, bool pressed,
                                         unsigned int state) noexcept {
    std::optional<std::uint32_t> code = named_key_for(keysym);
    // Printable ASCII keysyms are their own code points -- the text-host
    // convention, space included (0x20, not the evdev 57).
    if (!code.has_value() && keysym >= 0x20 && keysym <= 0x7e) {
        code = keysym;
    }
    if (!code.has_value()) {
        return std::nullopt;
    }
    const host::Modifiers modifiers = modifiers_from_state(state);
    if (pressed) {
        return host::Event{host::KeyDown{.code = *code, .modifiers = modifiers}};
    }
    return host::Event{host::KeyUp{.code = *code, .modifiers = modifiers}};
}

std::optional<host::Event> translate_button(const XButtonEvent& event) noexcept {
    const host::Modifiers modifiers = modifiers_from_state(event.state);
    const base::PointI pos{event.x, event.y};

    if (event.button >= Button4 && event.button <= kWheelRight) {
        if (event.type != ButtonPress) {
            return std::nullopt; // the release of a wheel notch is not a second notch
        }
        std::int32_t dx = 0;
        std::int32_t dy = 0;
        switch (event.button) {
            case Button4:
                dy = -1;
                break;
            case Button5:
                dy = 1;
                break;
            case kWheelLeft:
                dx = -1;
                break;
            case kWheelRight:
                dx = 1;
                break;
            default:
                return std::nullopt;
        }
        return host::Event{
            host::PointerWheel{.dx = dx, .dy = dy, .pos = pos, .modifiers = modifiers}};
    }

    if (event.button < Button1 || event.button > Button3) {
        return std::nullopt;
    }
    if (event.type == ButtonPress) {
        return host::Event{
            host::PointerDown{.button = event.button, .pos = pos, .modifiers = modifiers}};
    }
    if (event.type == ButtonRelease) {
        return host::Event{host::PointerUp{.button = event.button, .pos = pos}};
    }
    return std::nullopt;
}

host::Event translate_motion(const XMotionEvent& event) noexcept {
    return host::Event{host::PointerMove{.pos = base::PointI{event.x, event.y}}};
}

} // namespace visor::host::x11
