#pragma once

#include <cstdint>
#include <optional>

#include "visor/bindings/events.hpp"

namespace visor::host::cinux {

/// The /dev/event0 wire format, one event per read. The field layout mirrors
/// the kernel's struct (kernel/gui/event.hpp in the CinuxOS tree); visor
/// owns this mirror so the adapter reads raw bytes and translates, and a
/// layout drift shows up as a failed translation, not a silent misparse.
enum class WireType : std::uint8_t {
    Move = 0,
    MouseDown = 1,
    MouseUp = 2,
    KeyDown = 3,
    KeyUp = 4,
};

struct WireMouse {
    std::int32_t x;
    std::int32_t y;
    std::int32_t dx;
    std::int32_t dy;
    std::uint8_t buttons;
    bool left;
    bool right;
    bool middle;
};

struct WireKey {
    char ascii;
    std::uint8_t scancode;
    bool pressed;
    bool shift;
    bool ctrl;
    bool alt;
};

struct WireEvent {
    WireType type;
    union {
        WireMouse mouse;
        WireKey key;
    };
};

/**
 * @brief Translates one wire event into the core's Event, pure logic.
 *
 * Mouse words become pointer moves and presses at the wire's absolute
 * position, button 1. Key words become presses or releases; the code is the
 * ASCII byte when the kernel filled one (printable text rides the code-point
 * convention), otherwise a small table maps the scancodes the core names
 * (Backspace, Tab, Enter, Space on their set-1 values, the arrows on the HID
 * usage ids the kernel emits for them). A key with neither an ascii nor a
 * known scancode, or a type out of range, translates to nothing -- the
 * adapter drops it, the same honest refusal as an empty table entry.
 */
[[nodiscard]] std::optional<host::Event> translate_event(const WireEvent& wire) noexcept;

/// The scancode half of the table, exposed for the adapter's documentation
/// and the judgments: which raw codes map onto the core's named keys.
[[nodiscard]] std::optional<std::uint32_t> named_key_for(std::uint8_t scancode) noexcept;

} // namespace visor::host::cinux
