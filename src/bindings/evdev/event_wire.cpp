#include "visor/bindings/evdev/event_wire.hpp"

#include <cstdint>
#include <limits>
#include <optional>

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"

namespace visor::host::evdev {
namespace {

// Buttons live at 0x100 and above; keyboard codes stay below. A button the
// translator does not know is refused rather than leaked as a key.
constexpr std::uint16_t kButtonBandStart = 0x100;

std::optional<std::uint32_t> named_key(std::uint16_t code) noexcept {
    switch (code) {
        case kKeyUp:
            return host::kKeyUp;
        case kKeyDown:
            return host::kKeyDown;
        case kKeyLeft:
            return host::kKeyLeft;
        case kKeyRight:
            return host::kKeyRight;
        default:
            return std::nullopt;
    }
}

std::uint32_t button_number(std::uint16_t code) noexcept {
    switch (code) {
        case kBtnLeft:
            return 1;
        case kBtnRight:
            return 2;
        case kBtnMiddle:
            return 3;
        default:
            return 0;
    }
}

// One position per physical modifier key. The two halves of a modifier share
// a named flag but not a position, so overlap and release order stay honest.
// Positions feed base::bit's mask vocabulary; no raw mask literal here.
constexpr std::uint8_t kLeftShiftPosition = 0;
constexpr std::uint8_t kRightShiftPosition = 1;
constexpr std::uint8_t kLeftCtrlPosition = 2;
constexpr std::uint8_t kRightCtrlPosition = 3;
constexpr std::uint8_t kLeftAltPosition = 4;
constexpr std::uint8_t kRightAltPosition = 5;
constexpr std::uint8_t kLeftMetaPosition = 6;
constexpr std::uint8_t kRightMetaPosition = 7;
constexpr std::uint8_t kNoModifierPosition = 0xFF;

std::uint8_t modifier_position(std::uint16_t code) noexcept {
    switch (code) {
        case kKeyLeftShift:
            return kLeftShiftPosition;
        case kKeyRightShift:
            return kRightShiftPosition;
        case kKeyLeftCtrl:
            return kLeftCtrlPosition;
        case kKeyRightCtrl:
            return kRightCtrlPosition;
        case kKeyLeftAlt:
            return kLeftAltPosition;
        case kKeyRightAlt:
            return kRightAltPosition;
        case kKeyLeftMeta:
            return kLeftMetaPosition;
        case kKeyRightMeta:
            return kRightMetaPosition;
        default:
            return kNoModifierPosition;
    }
}

base::bit::BitMask<std::uint8_t> modifier_mask(std::uint8_t first, std::uint8_t second) noexcept {
    return base::bit::mask_bit<std::uint8_t>(first) | base::bit::mask_bit<std::uint8_t>(second);
}

// Relative moves are int deltas added to an int position. Doing the add in
// 64 bits keeps a hostile or broken word from overflowing before the clamp.
int clamped_axis(int current, std::int32_t delta, int extent) noexcept {
    if (extent <= 0) {
        return 0;
    }
    const std::int64_t next = static_cast<std::int64_t>(current) + delta;
    const std::int64_t last = static_cast<std::int64_t>(extent) - 1;
    if (next < 0) {
        return 0;
    }
    if (next > last) {
        return static_cast<int>(last);
    }
    return static_cast<int>(next);
}

// The core says wheel up is negative dy. Negating INT32_MIN would overflow,
// so that one value saturates at the opposite end of the range.
std::int32_t negated_steps(std::int32_t value) noexcept {
    if (value == std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return -value;
}

} // namespace

EvdevState::EvdevState(base::SizeI screen) noexcept : screen_{screen} {}

void EvdevState::set_modifier(std::uint16_t code, bool pressed) noexcept {
    const std::uint8_t position = modifier_position(code);
    if (position == kNoModifierPosition) {
        return;
    }
    held_modifiers_.deposit(base::bit::bit_at(position),
                            pressed ? std::uint8_t{1} : std::uint8_t{0});
    modifiers_.shift = held_modifiers_.has(modifier_mask(kLeftShiftPosition, kRightShiftPosition));
    modifiers_.ctrl = held_modifiers_.has(modifier_mask(kLeftCtrlPosition, kRightCtrlPosition));
    modifiers_.alt = held_modifiers_.has(modifier_mask(kLeftAltPosition, kRightAltPosition));
    modifiers_.super = held_modifiers_.has(modifier_mask(kLeftMetaPosition, kRightMetaPosition));
}

std::optional<host::Event> translate_event(EvdevState& state, const WireEvent& wire) noexcept {
    switch (wire.type) {
        case kEvSyn:
            return std::nullopt;
        case kEvKey: {
            if (wire.value < 0 || wire.value > 2) {
                return std::nullopt;
            }
            if (wire.code >= kButtonBandStart) {
                const std::uint32_t button = button_number(wire.code);
                if (button == 0 || wire.value == 2) {
                    return std::nullopt;
                }
                if (wire.value == 1) {
                    return host::Event{host::PointerDown{
                        .button = button,
                        .pos = state.pointer_,
                        .modifiers = state.modifiers_,
                    }};
                }
                return host::Event{host::PointerUp{
                    .button = button,
                    .pos = state.pointer_,
                }};
            }
            if (wire.value != 2) {
                state.set_modifier(wire.code, wire.value == 1);
            }
            std::uint32_t code = wire.code;
            if (const auto named = named_key(wire.code)) {
                code = *named;
            }
            if (wire.value != 0) {
                return host::Event{host::KeyDown{
                    .code = code,
                    .modifiers = state.modifiers_,
                }};
            }
            return host::Event{host::KeyUp{
                .code = code,
                .modifiers = state.modifiers_,
            }};
        }
        case kEvRel: {
            switch (wire.code) {
                case kRelX:
                    state.pointer_.x = clamped_axis(state.pointer_.x, wire.value, state.screen_.w);
                    return host::Event{host::PointerMove{.pos = state.pointer_}};
                case kRelY:
                    state.pointer_.y = clamped_axis(state.pointer_.y, wire.value, state.screen_.h);
                    return host::Event{host::PointerMove{.pos = state.pointer_}};
                case kRelWheel:
                    return host::Event{host::PointerWheel{
                        .dx = 0,
                        .dy = negated_steps(wire.value),
                        .pos = state.pointer_,
                        .modifiers = state.modifiers_,
                    }};
                case kRelHWheel:
                    return host::Event{host::PointerWheel{
                        .dx = wire.value,
                        .dy = 0,
                        .pos = state.pointer_,
                        .modifiers = state.modifiers_,
                    }};
                default:
                    return std::nullopt;
            }
        }
        default:
            return std::nullopt;
    }
}

} // namespace visor::host::evdev
