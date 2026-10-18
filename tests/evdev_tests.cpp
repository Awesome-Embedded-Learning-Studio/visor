#include "cases.hpp"
#include "testing.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/evdev/event_wire.hpp"
#include "visor/bindings/events.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

namespace {

using visor::base::PointI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::KeyDown;
using visor::host::KeyUp;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::host::PointerWheel;
using visor::host::evdev::EvdevState;
using visor::host::evdev::kBtnLeft;
using visor::host::evdev::kBtnMiddle;
using visor::host::evdev::kBtnRight;
using visor::host::evdev::kEvKey;
using visor::host::evdev::kEvRel;
using visor::host::evdev::kEvSyn;
using visor::host::evdev::kKeyBackspace;
using visor::host::evdev::kKeyDown;
using visor::host::evdev::kKeyEnter;
using visor::host::evdev::kKeyLeft;
using visor::host::evdev::kKeyLeftAlt;
using visor::host::evdev::kKeyLeftCtrl;
using visor::host::evdev::kKeyLeftMeta;
using visor::host::evdev::kKeyLeftShift;
using visor::host::evdev::kKeyRight;
using visor::host::evdev::kKeyRightAlt;
using visor::host::evdev::kKeyRightCtrl;
using visor::host::evdev::kKeyRightMeta;
using visor::host::evdev::kKeyRightShift;
using visor::host::evdev::kKeySpace;
using visor::host::evdev::kKeyTab;
using visor::host::evdev::kKeyUp;
using visor::host::evdev::kRelHWheel;
using visor::host::evdev::kRelWheel;
using visor::host::evdev::kRelX;
using visor::host::evdev::kRelY;
using visor::host::evdev::WireEvent;

WireEvent key(std::uint16_t code, std::int32_t value) noexcept {
    return WireEvent{.type = kEvKey, .code = code, .value = value};
}

WireEvent rel(std::uint16_t code, std::int32_t value) noexcept {
    return WireEvent{.type = kEvRel, .code = code, .value = value};
}

std::optional<Event> step(EvdevState& state, WireEvent wire) noexcept {
    return visor::host::evdev::translate_event(state, wire);
}

const KeyDown* as_key_down(const std::optional<Event>& event) noexcept {
    return event.has_value() ? std::get_if<KeyDown>(&*event) : nullptr;
}

const KeyUp* as_key_up(const std::optional<Event>& event) noexcept {
    return event.has_value() ? std::get_if<KeyUp>(&*event) : nullptr;
}

const PointerDown* as_pointer_down(const std::optional<Event>& event) noexcept {
    return event.has_value() ? std::get_if<PointerDown>(&*event) : nullptr;
}

const PointerUp* as_pointer_up(const std::optional<Event>& event) noexcept {
    return event.has_value() ? std::get_if<PointerUp>(&*event) : nullptr;
}

const PointerMove* as_pointer_move(const std::optional<Event>& event) noexcept {
    return event.has_value() ? std::get_if<PointerMove>(&*event) : nullptr;
}

const PointerWheel* as_wheel(const std::optional<Event>& event) noexcept {
    return event.has_value() ? std::get_if<PointerWheel>(&*event) : nullptr;
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_evdev_tests() {
    // Modifiers: a shift press updates the state before its own event, the
    // following key carries the flag, a repeat keeps it, and the release
    // clears both. A key with no modifier hears none.
    {
        EvdevState state(SizeI{.w = 100, .h = 50});
        const auto shift_down = step(state, key(kKeyLeftShift, 1));
        const auto* shift_word = as_key_down(shift_down);
        VISOR_CHECK(shift_word != nullptr);
        if (shift_word != nullptr) {
            VISOR_CHECK_EQ(shift_word->code, static_cast<std::uint32_t>(kKeyLeftShift));
            VISOR_CHECK(shift_word->modifiers.shift);
        }

        const std::uint16_t key_a = 30; // KEY_A
        const auto a_down = step(state, key(key_a, 1));
        const auto* a_word = as_key_down(a_down);
        VISOR_CHECK(a_word != nullptr);
        if (a_word != nullptr) {
            VISOR_CHECK_EQ(a_word->code, static_cast<std::uint32_t>(key_a));
            VISOR_CHECK(a_word->modifiers.shift);
            VISOR_CHECK(!a_word->modifiers.ctrl);
            VISOR_CHECK(!a_word->modifiers.alt);
            VISOR_CHECK(!a_word->modifiers.super);
        }

        const auto a_repeat = step(state, key(key_a, 2));
        const auto* repeat_word = as_key_down(a_repeat);
        VISOR_CHECK(repeat_word != nullptr);
        if (repeat_word != nullptr) {
            VISOR_CHECK(repeat_word->modifiers.shift);
        }

        const auto a_up = step(state, key(key_a, 0));
        const auto* a_up_word = as_key_up(a_up);
        VISOR_CHECK(a_up_word != nullptr);
        if (a_up_word != nullptr) {
            VISOR_CHECK_EQ(a_up_word->code, static_cast<std::uint32_t>(key_a));
            VISOR_CHECK(a_up_word->modifiers.shift);
        }

        const auto shift_up = step(state, key(kKeyLeftShift, 0));
        const auto* shift_up_word = as_key_up(shift_up);
        VISOR_CHECK(shift_up_word != nullptr);
        if (shift_up_word != nullptr) {
            VISOR_CHECK(!shift_up_word->modifiers.shift);
        }

        const auto plain_a = step(state, key(key_a, 1));
        const auto* plain_word = as_key_down(plain_a);
        VISOR_CHECK(plain_word != nullptr);
        if (plain_word != nullptr) {
            VISOR_CHECK(!plain_word->modifiers.shift);
            VISOR_CHECK(!plain_word->modifiers.ctrl);
            VISOR_CHECK(!plain_word->modifiers.alt);
            VISOR_CHECK(!plain_word->modifiers.super);
        }
    }

    // Every modifier key sets its named flag, from either hand. Releasing it
    // clears the flag again.
    {
        struct ModifierCase {
            std::uint16_t code;
            bool shift;
            bool ctrl;
            bool alt;
            bool super;
        };
        const std::array<ModifierCase, 8> cases{
            {
                {.code = kKeyLeftShift, .shift = true, .ctrl = false, .alt = false, .super = false},
                {
                    .code = kKeyRightShift,
                    .shift = true,
                    .ctrl = false,
                    .alt = false,
                    .super = false,
                },
                {.code = kKeyLeftCtrl, .shift = false, .ctrl = true, .alt = false, .super = false},
                {.code = kKeyRightCtrl, .shift = false, .ctrl = true, .alt = false, .super = false},
                {.code = kKeyLeftAlt, .shift = false, .ctrl = false, .alt = true, .super = false},
                {.code = kKeyRightAlt, .shift = false, .ctrl = false, .alt = true, .super = false},
                {.code = kKeyLeftMeta, .shift = false, .ctrl = false, .alt = false, .super = true},
                {.code = kKeyRightMeta, .shift = false, .ctrl = false, .alt = false, .super = true},
            },
        };
        for (const ModifierCase& entry : cases) {
            EvdevState state(SizeI{.w = 10, .h = 10});
            const auto down = step(state, key(entry.code, 1));
            const auto* word = as_key_down(down);
            VISOR_CHECK(word != nullptr);
            if (word != nullptr) {
                VISOR_CHECK_EQ(word->modifiers.shift, entry.shift);
                VISOR_CHECK_EQ(word->modifiers.ctrl, entry.ctrl);
                VISOR_CHECK_EQ(word->modifiers.alt, entry.alt);
                VISOR_CHECK_EQ(word->modifiers.super, entry.super);
            }
            const auto up = step(state, key(entry.code, 0));
            const auto* up_word = as_key_up(up);
            VISOR_CHECK(up_word != nullptr);
            if (up_word != nullptr) {
                VISOR_CHECK(!up_word->modifiers.shift);
                VISOR_CHECK(!up_word->modifiers.ctrl);
                VISOR_CHECK(!up_word->modifiers.alt);
                VISOR_CHECK(!up_word->modifiers.super);
            }
        }
    }

    // Both halves of a modifier are tracked separately: releasing one hand
    // leaves the flag set while the other hand still holds it.
    {
        EvdevState state(SizeI{.w = 10, .h = 10});
        static_cast<void>(step(state, key(kKeyLeftShift, 1)));
        static_cast<void>(step(state, key(kKeyRightShift, 1)));
        const auto release_left = step(state, key(kKeyLeftShift, 0));
        const auto* left_word = as_key_up(release_left);
        VISOR_CHECK(left_word != nullptr);
        if (left_word != nullptr) {
            VISOR_CHECK(left_word->modifiers.shift);
        }
        const auto release_right = step(state, key(kKeyRightShift, 0));
        const auto* right_word = as_key_up(release_right);
        VISOR_CHECK(right_word != nullptr);
        if (right_word != nullptr) {
            VISOR_CHECK(!right_word->modifiers.shift);
        }
    }

    // The named keys: the editing keys already share the core's numbers,
    // the arrows are remapped off their printable-ASCII collisions.
    {
        struct NamedKey {
            std::uint16_t wire;
            std::uint32_t core;
        };
        const std::array<NamedKey, 8> named{
            {
                {.wire = kKeyBackspace, .core = visor::host::kKeyBackspace},
                {.wire = kKeyTab, .core = visor::host::kKeyTab},
                {.wire = kKeyEnter, .core = visor::host::kKeyEnter},
                {.wire = kKeySpace, .core = visor::host::kKeySpace},
                {.wire = kKeyUp, .core = visor::host::kKeyUp},
                {.wire = kKeyDown, .core = visor::host::kKeyDown},
                {.wire = kKeyLeft, .core = visor::host::kKeyLeft},
                {.wire = kKeyRight, .core = visor::host::kKeyRight},
            },
        };
        for (const NamedKey& entry : named) {
            EvdevState state(SizeI{.w = 10, .h = 10});
            const auto event = step(state, key(entry.wire, 1));
            const auto* word = as_key_down(event);
            VISOR_CHECK(word != nullptr);
            if (word != nullptr) {
                VISOR_CHECK_EQ(word->code, entry.core);
            }
        }
    }

    // Relative moves accumulate one axis at a time and clamp at both edges.
    // A zero-sized screen clamps everything to the origin.
    {
        EvdevState state(SizeI{.w = 10, .h = 6});
        const auto first = step(state, rel(kRelX, 3));
        const auto* first_word = as_pointer_move(first);
        VISOR_CHECK(first_word != nullptr);
        if (first_word != nullptr) {
            VISOR_CHECK_EQ(first_word->pos, (PointI{3, 0}));
        }

        const auto second = step(state, rel(kRelY, 2));
        const auto* second_word = as_pointer_move(second);
        VISOR_CHECK(second_word != nullptr);
        if (second_word != nullptr) {
            VISOR_CHECK_EQ(second_word->pos, (PointI{3, 2}));
        }

        const auto far_right = step(state, rel(kRelX, 100));
        const auto* far_right_word = as_pointer_move(far_right);
        VISOR_CHECK(far_right_word != nullptr);
        if (far_right_word != nullptr) {
            VISOR_CHECK_EQ(far_right_word->pos, (PointI{9, 2}));
        }

        const auto far_down = step(state, rel(kRelY, std::numeric_limits<std::int32_t>::max()));
        const auto* far_down_word = as_pointer_move(far_down);
        VISOR_CHECK(far_down_word != nullptr);
        if (far_down_word != nullptr) {
            VISOR_CHECK_EQ(far_down_word->pos, (PointI{9, 5}));
        }

        const auto far_left = step(state, rel(kRelX, -100));
        const auto* far_left_word = as_pointer_move(far_left);
        VISOR_CHECK(far_left_word != nullptr);
        if (far_left_word != nullptr) {
            VISOR_CHECK_EQ(far_left_word->pos, (PointI{0, 5}));
        }

        const auto far_up = step(state, rel(kRelY, -100));
        const auto* far_up_word = as_pointer_move(far_up);
        VISOR_CHECK(far_up_word != nullptr);
        if (far_up_word != nullptr) {
            VISOR_CHECK_EQ(far_up_word->pos, (PointI{0, 0}));
        }

        EvdevState empty(SizeI{.w = 0, .h = 0});
        const auto empty_move = step(empty, rel(kRelX, 5));
        const auto* empty_word = as_pointer_move(empty_move);
        VISOR_CHECK(empty_word != nullptr);
        if (empty_word != nullptr) {
            VISOR_CHECK_EQ(empty_word->pos, (PointI{0, 0}));
        }
    }

    // Mouse buttons use the tracked position and the modifiers held at the
    // press. A repeat on a button is dropped, and an unknown button in the
    // BTN band is refused instead of leaking through as a key.
    {
        EvdevState state(SizeI{.w = 20, .h = 10});
        static_cast<void>(step(state, rel(kRelX, 5)));
        static_cast<void>(step(state, rel(kRelY, 4)));
        static_cast<void>(step(state, key(kKeyLeftShift, 1)));

        const auto left_down = step(state, key(kBtnLeft, 1));
        const auto* left_word = as_pointer_down(left_down);
        VISOR_CHECK(left_word != nullptr);
        if (left_word != nullptr) {
            VISOR_CHECK_EQ(left_word->button, 1U);
            VISOR_CHECK_EQ(left_word->pos, (PointI{5, 4}));
            VISOR_CHECK(left_word->modifiers.shift);
        }

        const auto left_up = step(state, key(kBtnLeft, 0));
        const auto* left_up_word = as_pointer_up(left_up);
        VISOR_CHECK(left_up_word != nullptr);
        if (left_up_word != nullptr) {
            VISOR_CHECK_EQ(left_up_word->button, 1U);
            VISOR_CHECK_EQ(left_up_word->pos, (PointI{5, 4}));
        }

        const auto right_down = step(state, key(kBtnRight, 1));
        const auto* right_word = as_pointer_down(right_down);
        VISOR_CHECK(right_word != nullptr);
        if (right_word != nullptr) {
            VISOR_CHECK_EQ(right_word->button, 2U);
        }

        const auto middle_down = step(state, key(kBtnMiddle, 1));
        const auto* middle_word = as_pointer_down(middle_down);
        VISOR_CHECK(middle_word != nullptr);
        if (middle_word != nullptr) {
            VISOR_CHECK_EQ(middle_word->button, 3U);
        }

        const auto left_repeat = step(state, key(kBtnLeft, 2));
        VISOR_CHECK(!left_repeat.has_value());
        const std::uint16_t btn_side = 0x113;
        const auto side_down = step(state, key(btn_side, 1));
        VISOR_CHECK(!side_down.has_value());
    }

    // The wheel: one word per notch, value as detent count, sign normalized
    // to the core (up is negative dy, right is positive dx). The word carries
    // the tracked position and modifiers.
    {
        EvdevState state(SizeI{.w = 20, .h = 10});
        static_cast<void>(step(state, rel(kRelX, 5)));
        static_cast<void>(step(state, rel(kRelY, 4)));
        static_cast<void>(step(state, key(kKeyLeftAlt, 1)));

        const auto up = step(state, rel(kRelWheel, 1));
        const auto* up_word = as_wheel(up);
        VISOR_CHECK(up_word != nullptr);
        if (up_word != nullptr) {
            VISOR_CHECK_EQ(up_word->dx, 0);
            VISOR_CHECK_EQ(up_word->dy, -1);
            VISOR_CHECK_EQ(up_word->pos, (PointI{5, 4}));
            VISOR_CHECK(up_word->modifiers.alt);
        }

        const auto down = step(state, rel(kRelWheel, -1));
        const auto* down_word = as_wheel(down);
        VISOR_CHECK(down_word != nullptr);
        if (down_word != nullptr) {
            VISOR_CHECK_EQ(down_word->dy, 1);
        }

        const auto right = step(state, rel(kRelHWheel, 1));
        const auto* right_word = as_wheel(right);
        VISOR_CHECK(right_word != nullptr);
        if (right_word != nullptr) {
            VISOR_CHECK_EQ(right_word->dx, 1);
            VISOR_CHECK_EQ(right_word->dy, 0);
        }

        const auto left = step(state, rel(kRelHWheel, -1));
        const auto* left_word = as_wheel(left);
        VISOR_CHECK(left_word != nullptr);
        if (left_word != nullptr) {
            VISOR_CHECK_EQ(left_word->dx, -1);
        }

        const auto two_steps = step(state, rel(kRelWheel, 2));
        const auto* two_word = as_wheel(two_steps);
        VISOR_CHECK(two_word != nullptr);
        if (two_word != nullptr) {
            VISOR_CHECK_EQ(two_word->dy, -2);
        }

        const auto huge = step(state, rel(kRelWheel, std::numeric_limits<std::int32_t>::min()));
        const auto* huge_word = as_wheel(huge);
        VISOR_CHECK(huge_word != nullptr);
        if (huge_word != nullptr) {
            VISOR_CHECK_EQ(huge_word->dy, std::numeric_limits<std::int32_t>::max());
        }
    }

    // Synchronization, unknown types, unknown relative axes and EV_KEY values
    // outside 0 to 2 translate to nothing.
    {
        EvdevState state(SizeI{.w = 10, .h = 10});
        VISOR_CHECK(!step(state, WireEvent{.type = kEvSyn, .code = 0, .value = 0}).has_value());
        VISOR_CHECK(!step(state, WireEvent{.type = kEvSyn, .code = 3, .value = 0}).has_value());
        constexpr std::uint16_t unknown_type = 0x99;
        VISOR_CHECK(
            !step(state, WireEvent{.type = unknown_type, .code = 0, .value = 1}).has_value());
        constexpr std::uint16_t unknown_axis = 0x2f;
        VISOR_CHECK(!step(state, rel(unknown_axis, 1)).has_value());
        VISOR_CHECK(!step(state, key(30, 7)).has_value());
    }
}
