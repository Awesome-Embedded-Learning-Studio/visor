#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/bindings/wayland/event_wire.hpp"
#include "visor/bindings/wayland/key_repeat.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace {

using visor::host::Event;
using visor::host::KeyDown;
using visor::host::KeyUp;
using visor::host::Modifiers;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::host::PointerWheel;
using visor::host::wayland::translate_button;
using visor::host::wayland::translate_key;
using visor::host::wayland::translate_modifiers;
using visor::host::wayland::translate_motion;
using visor::host::wayland::translate_wheel;

constexpr Modifiers kPlain{};
constexpr Modifiers kFull{.shift = true, .ctrl = true, .alt = true, .super = true};

/// Reads the word as one key press of the given code, or reports a miss.
std::uint32_t key_code(const std::optional<Event>& word) {
    if (!word.has_value() || !std::holds_alternative<KeyDown>(*word)) {
        return 0xffff'ffff;
    }
    return std::get<KeyDown>(*word).code;
}

} // namespace

void run_wayland_tests() {
    using visor::base::PointI;
    using visor::host::wayland::KeyRepeat;

    // Backspace repeats only after the compositor's delay, then at its rate.
    {
        KeyRepeat repeat;
        repeat.configure(25, 400, 0);
        repeat.key(14, true, true, 100'000);
        VISOR_CHECK(!repeat.poll(499'999).has_value());
        VISOR_CHECK_EQ(repeat.poll(500'000), 14U);
        VISOR_CHECK(!repeat.poll(500'000).has_value());
        VISOR_CHECK(!repeat.poll(539'999).has_value());
        VISOR_CHECK_EQ(repeat.poll(540'000), 14U);
        repeat.key(14, false, true, 550'000);
        VISOR_CHECK(!repeat.poll(1'000'000).has_value());
    }

    // The last repeatable press owns the timer. Other releases and modifier
    // presses leave it alone; releasing its owner does not resume an old key.
    {
        KeyRepeat repeat;
        repeat.configure(20, 50, 0);
        repeat.key(30, true, true, 0);
        repeat.key(48, true, true, 20'000);
        repeat.key(30, false, true, 30'000);
        repeat.key(42, true, false, 40'000);
        VISOR_CHECK(!repeat.poll(69'999).has_value());
        VISOR_CHECK_EQ(repeat.poll(70'000), 48U);
        repeat.key(48, false, true, 80'000);
        VISOR_CHECK(!repeat.poll(200'000).has_value());
    }

    // A stalled application emits one repeat on waking, keeping its cadence
    // without flooding the queue with every interval it missed.
    {
        KeyRepeat repeat;
        repeat.configure(25, 0, 0);
        repeat.key(30, true, true, 1'000'000);
        VISOR_CHECK_EQ(repeat.poll(1'210'000), 30U);
        VISOR_CHECK(!repeat.poll(1'210'000).has_value());
        VISOR_CHECK(!repeat.poll(1'239'999).has_value());
        VISOR_CHECK_EQ(repeat.poll(1'240'000), 30U);
    }

    // Focus loss and keyboard removal both cancel the pending press. A
    // modifier or lock key alone never starts repetition.
    {
        KeyRepeat repeat;
        repeat.key(14, true, true, 0);
        repeat.cancel();
        VISOR_CHECK(!repeat.poll(1'000'000).has_value());
        repeat.key(42, true, false, 1'000'000);
        VISOR_CHECK(!repeat.poll(2'000'000).has_value());
        repeat.key(14, true, true, 2'000'000);
        repeat.cancel();
        VISOR_CHECK(!repeat.poll(3'000'000).has_value());
    }

    // A zero rate disables both the current repeat and future presses.
    // Re-enabling requires a fresh press, so an old held key cannot revive.
    {
        KeyRepeat repeat;
        repeat.key(14, true, true, 0);
        repeat.configure(0, 400, 100'000);
        VISOR_CHECK(!repeat.poll(1'000'000).has_value());
        repeat.key(30, true, true, 1'000'000);
        VISOR_CHECK(!repeat.poll(2'000'000).has_value());
        repeat.configure(25, 400, 2'000'000);
        VISOR_CHECK(!repeat.poll(3'000'000).has_value());
        repeat.key(30, true, true, 3'000'000);
        VISOR_CHECK_EQ(repeat.poll(3'400'000), 30U);
    }

    // A repeat_info update while held restarts the delay using the new
    // settings. Invalid settings safely disable repetition.
    {
        KeyRepeat repeat;
        repeat.configure(25, 400, 0);
        repeat.key(14, true, true, 0);
        repeat.configure(10, 100, 200'000);
        VISOR_CHECK(!repeat.poll(299'999).has_value());
        VISOR_CHECK_EQ(repeat.poll(300'000), 14U);
        VISOR_CHECK_EQ(repeat.poll(400'000), 14U);
        repeat.configure(-1, 100, 400'000);
        VISOR_CHECK(!repeat.poll(1'000'000).has_value());
        repeat.configure(25, -1, 1'000'000);
        repeat.key(14, true, true, 1'000'000);
        VISOR_CHECK(!repeat.poll(2'000'000).has_value());
    }

    // The mask halves: each named bit lights its own flag and none other,
    // and the lock and numeric bits light nothing.
    {
        const Modifiers none = translate_modifiers(0x00);
        VISOR_CHECK_EQ(none.shift, false);
        VISOR_CHECK_EQ(none.ctrl, false);
        VISOR_CHECK_EQ(none.alt, false);
        VISOR_CHECK_EQ(none.super, false);
        VISOR_CHECK_EQ(translate_modifiers(visor::host::wayland::kModShift).shift, true);
        VISOR_CHECK_EQ(translate_modifiers(visor::host::wayland::kModShift).ctrl, false);
        VISOR_CHECK_EQ(translate_modifiers(visor::host::wayland::kModControl).ctrl, true);
        VISOR_CHECK_EQ(translate_modifiers(visor::host::wayland::kModAlt).alt, true);
        VISOR_CHECK_EQ(translate_modifiers(visor::host::wayland::kModSuper).super, true);
        const Modifiers both = translate_modifiers(visor::host::wayland::kModShift |
                                                   visor::host::wayland::kModControl);
        VISOR_CHECK_EQ(both.shift, true);
        VISOR_CHECK_EQ(both.ctrl, true);
        VISOR_CHECK_EQ(both.alt, false);
        const Modifiers locked =
            translate_modifiers(0x02U | 0x10U | 0x20U | 0x80U); // lock, num, mod3, mod5
        VISOR_CHECK_EQ(locked.shift, false);
        VISOR_CHECK_EQ(locked.ctrl, false);
        VISOR_CHECK_EQ(locked.alt, false);
        VISOR_CHECK_EQ(locked.super, false);
    }

    // Every unnamed key rides its evdev code unchanged, press and release
    // alike, carrying whatever modifiers were held.
    {
        const auto a_down = translate_key(30, true, kPlain); // evdev 'a'
        VISOR_CHECK(a_down.has_value());
        if (!a_down.has_value()) {
            return;
        }
        VISOR_CHECK_EQ(key_code(a_down), 30U);
        VISOR_CHECK(std::holds_alternative<KeyDown>(*a_down));

        const auto a_up = translate_key(30, false, kFull);
        VISOR_CHECK(a_up.has_value());
        if (!a_up.has_value()) {
            return;
        }
        VISOR_CHECK(std::holds_alternative<KeyUp>(*a_up));
        VISOR_CHECK_EQ(std::get<KeyUp>(*a_up).code, 30U);
        VISOR_CHECK_EQ(std::get<KeyUp>(*a_up).modifiers.ctrl, true);

        // The core's named evdev keys share their values already.
        VISOR_CHECK_EQ(key_code(translate_key(14, true, kPlain)), visor::host::kKeyBackspace);
        VISOR_CHECK_EQ(key_code(translate_key(15, true, kPlain)), visor::host::kKeyTab);
        VISOR_CHECK_EQ(key_code(translate_key(28, true, kPlain)), visor::host::kKeyEnter);
        VISOR_CHECK_EQ(key_code(translate_key(57, true, kPlain)), visor::host::kKeySpace);

        // The arrows take the core's out-of-band values instead.
        VISOR_CHECK_EQ(key_code(translate_key(103, true, kPlain)), visor::host::kKeyUp);
        VISOR_CHECK_EQ(key_code(translate_key(108, true, kPlain)), visor::host::kKeyDown);
        VISOR_CHECK_EQ(key_code(translate_key(105, true, kPlain)), visor::host::kKeyLeft);
        VISOR_CHECK_EQ(key_code(translate_key(106, true, kPlain)), visor::host::kKeyRight);

        // A modifier key is a key like any other: its own word passes through.
        VISOR_CHECK_EQ(key_code(translate_key(42, true, kPlain)), 42U); // left shift
    }

    // The three known buttons take the kernel's own numbering, asserted
    // against the raw evdev words so a flipped constant cannot pass by
    // testing itself; side buttons and beyond translate to nothing.
    {
        const PointI at{40, 12};
        const auto left = translate_button(0x110, true, at, kFull); // BTN_LEFT
        VISOR_CHECK(left.has_value());
        if (!left.has_value()) {
            return;
        }
        VISOR_CHECK(std::holds_alternative<PointerDown>(*left));
        VISOR_CHECK_EQ(std::get<PointerDown>(*left).button, 1U);
        VISOR_CHECK_EQ(std::get<PointerDown>(*left).pos.x, 40);
        VISOR_CHECK_EQ(std::get<PointerDown>(*left).modifiers.shift, true);

        const auto right = translate_button(0x111, true, at, kPlain); // BTN_RIGHT
        VISOR_CHECK(right.has_value());
        if (!right.has_value()) {
            return;
        }
        VISOR_CHECK_EQ(std::get<PointerDown>(*right).button, 2U);

        const auto middle = translate_button(0x112, false, at, kPlain); // BTN_MIDDLE
        VISOR_CHECK(middle.has_value());
        if (!middle.has_value()) {
            return;
        }
        VISOR_CHECK(std::holds_alternative<PointerUp>(*middle));
        VISOR_CHECK_EQ(std::get<PointerUp>(*middle).button, 3U);

        VISOR_CHECK(!translate_button(0x113, true, at, kPlain).has_value()); // side button
        VISOR_CHECK(!translate_button(0x114, true, at, kPlain).has_value());
    }

    // Motion passes the position through, negative coordinates included --
    // out-of-bounds words are the core's to clip, not the host's.
    {
        const auto word = translate_motion(PointI{-4, 130});
        VISOR_CHECK(std::holds_alternative<PointerMove>(word));
        VISOR_CHECK_EQ(std::get<PointerMove>(word).pos.x, -4);
        VISOR_CHECK_EQ(std::get<PointerMove>(word).pos.y, 130);
    }

    // Wheel steps pass their signs through; an empty pair is no word at all.
    {
        const PointI at{8, 9};
        const auto down_two = translate_wheel(0, 2, at, kPlain);
        VISOR_CHECK(down_two.has_value());
        if (!down_two.has_value()) {
            return;
        }
        VISOR_CHECK(std::holds_alternative<PointerWheel>(*down_two));
        VISOR_CHECK_EQ(std::get<PointerWheel>(*down_two).dy, 2);
        VISOR_CHECK_EQ(std::get<PointerWheel>(*down_two).dx, 0);

        const auto diagonal = translate_wheel(-3, 1, at, kFull);
        VISOR_CHECK(diagonal.has_value());
        if (!diagonal.has_value()) {
            return;
        }
        VISOR_CHECK_EQ(std::get<PointerWheel>(*diagonal).dx, -3);
        VISOR_CHECK_EQ(std::get<PointerWheel>(*diagonal).dy, 1);
        VISOR_CHECK_EQ(std::get<PointerWheel>(*diagonal).modifiers.alt, true);

        VISOR_CHECK(!translate_wheel(0, 0, at, kPlain).has_value());
    }
}
