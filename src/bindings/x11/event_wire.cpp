#include "visor/bindings/x11/event_wire.hpp"

#include <cstdint>
#include <optional>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"

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

} // namespace

std::optional<host::Event> translate_key(std::uint32_t keysym, bool pressed) noexcept {
    std::optional<std::uint32_t> code = named_key_for(keysym);
    // Printable ASCII keysyms are their own code points -- the text-host
    // convention, space included (0x20, not the evdev 57).
    if (!code.has_value() && keysym >= 0x20 && keysym <= 0x7e) {
        code = keysym;
    }
    if (!code.has_value()) {
        return std::nullopt;
    }
    if (pressed) {
        return host::Event{host::KeyDown{.code = *code}};
    }
    return host::Event{host::KeyUp{.code = *code}};
}

std::optional<host::Event> translate_button(const XButtonEvent& event) noexcept {
    if (event.button < Button1 || event.button > Button3) {
        return std::nullopt; // the wheel (4 and up) scrolls, it is not a button word
    }
    if (event.type == ButtonPress) {
        return host::Event{
            host::PointerDown{.button = event.button, .pos = base::PointI{event.x, event.y}}};
    }
    if (event.type == ButtonRelease) {
        return host::Event{
            host::PointerUp{.button = event.button, .pos = base::PointI{event.x, event.y}}};
    }
    return std::nullopt;
}

host::Event translate_motion(const XMotionEvent& event) noexcept {
    return host::Event{host::PointerMove{.pos = base::PointI{event.x, event.y}}};
}

} // namespace visor::host::x11
