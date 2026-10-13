#include "visor/bindings/cinux/event_wire.hpp"

#include <cstdint>
#include <optional>

#include "visor/bindings/events.hpp"

namespace visor::host::cinux {
namespace {

// Set-1 make codes for the keys the kernel reports by scancode (no ASCII
// attached), plus the HID usage ids it emits for the arrows. The values the
// core names are deliberately NOT these numbers (the arrows' band would
// collide with printable ASCII), so the table is a true translation, not a
// pass-through.
struct ScancodeEntry {
    std::uint8_t scancode;
    std::uint32_t code;
};
// NOLINTNEXTLINE(modernize-avoid-c-arrays)
constexpr ScancodeEntry kScancodeTable[] = {
    {.scancode = 0x0e, .code = host::kKeyBackspace}, {.scancode = 0x0f, .code = host::kKeyTab},
    {.scancode = 0x1c, .code = host::kKeyEnter},     {.scancode = 0x39, .code = host::kKeySpace},
    {.scancode = 0x4f, .code = host::kKeyRight},     {.scancode = 0x50, .code = host::kKeyLeft},
    {.scancode = 0x51, .code = host::kKeyDown},      {.scancode = 0x52, .code = host::kKeyUp},
};

} // namespace

std::optional<std::uint32_t> named_key_for(std::uint8_t scancode) noexcept {
    for (const ScancodeEntry& entry : kScancodeTable) {
        if (entry.scancode == scancode) {
            return entry.code;
        }
    }
    return std::nullopt;
}

std::optional<host::Event> translate_event(const WireEvent& wire) noexcept {
    switch (wire.type) {
        case WireType::Move:
            return host::Event{host::PointerMove{.pos = {wire.mouse.x, wire.mouse.y}}};
        case WireType::MouseDown:
            return host::Event{host::PointerDown{.button = 1, .pos = {wire.mouse.x, wire.mouse.y}}};
        case WireType::MouseUp:
            return host::Event{host::PointerUp{.button = 1, .pos = {wire.mouse.x, wire.mouse.y}}};
        case WireType::KeyDown:
        case WireType::KeyUp: {
            // ASCII first: printable text rides the code-point convention.
            // A scancode-only key goes through the named table; neither is
            // nothing the core can use.
            std::uint32_t code = 0;
            if (wire.key.ascii != 0) {
                code = static_cast<std::uint8_t>(wire.key.ascii);
            } else if (const auto named = named_key_for(wire.key.scancode)) {
                code = *named;
            } else {
                return std::nullopt;
            }
            if (wire.type == WireType::KeyDown) {
                return host::Event{host::KeyDown{.code = code}};
            }
            return host::Event{host::KeyUp{.code = code}};
        }
    }
    return std::nullopt; // a type out of the wire's range
}

} // namespace visor::host::cinux
