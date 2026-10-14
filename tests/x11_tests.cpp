#include "cases.hpp"
#include "testing.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/x11/event_wire.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

void run_x11_tests() {
    using visor::host::KeyDown;
    using visor::host::KeyUp;
    using visor::host::PointerDown;
    using visor::host::PointerMove;
    using visor::host::PointerUp;

    // The printable band rides the code-point convention unchanged: 'a' goes
    // down as 0x61, '9' comes up as 57 -- the same number kKeySpace wears
    // under evdev, each consumer reading its own meaning.
    {
        const auto a = visor::host::x11::translate_key(XK_a, true);
        VISOR_CHECK(a.has_value());
        if (a.has_value()) {
            VISOR_CHECK(std::holds_alternative<KeyDown>(*a));
            VISOR_CHECK_EQ(std::get<KeyDown>(*a).code, 0x61U);
        }

        const auto nine = visor::host::x11::translate_key(XK_9, false);
        VISOR_CHECK(nine.has_value());
        if (nine.has_value()) {
            VISOR_CHECK(std::holds_alternative<KeyUp>(*nine));
            VISOR_CHECK_EQ(std::get<KeyUp>(*nine).code, 57U); // '9' as a code point
        }
    }

    // Space is the duality's live wire: a text host types the space bar as
    // its code point 0x20, the number activation consumers accept beside the
    // evdev 57 -- not kKeySpace's number.
    {
        const auto space = visor::host::x11::translate_key(XK_space, true);
        VISOR_CHECK(space.has_value());
        if (space.has_value()) {
            VISOR_CHECK(std::holds_alternative<KeyDown>(*space));
            VISOR_CHECK_EQ(std::get<KeyDown>(*space).code, visor::host::kCharSpace);
        }
    }

    // The named keys keep the core's numbers: Backspace, Tab and Enter on
    // their evdev values, press and release carrying the same code.
    {
        const auto back = visor::host::x11::translate_key(XK_BackSpace, true);
        VISOR_CHECK(back.has_value());
        if (back.has_value()) {
            VISOR_CHECK_EQ(std::get<KeyDown>(*back).code, visor::host::kKeyBackspace);
        }
        const auto tab = visor::host::x11::translate_key(XK_Tab, false);
        VISOR_CHECK(tab.has_value());
        if (tab.has_value()) {
            VISOR_CHECK_EQ(std::get<KeyUp>(*tab).code, visor::host::kKeyTab);
        }
        const auto enter = visor::host::x11::translate_key(XK_Return, true);
        VISOR_CHECK(enter.has_value());
        if (enter.has_value()) {
            VISOR_CHECK_EQ(std::get<KeyDown>(*enter).code, visor::host::kKeyEnter);
        }
    }

    // The arrows land out-of-band: the keysym numbers (0xff52 band) are not
    // what the core compares, the table is the bridge. Down and up share the
    // code, all four directions.
    {
        struct ArrowWord {
            std::uint32_t keysym;
            std::uint32_t code;
        };
        const std::array<ArrowWord, 4> arrows{
            {
                {.keysym = XK_Up, .code = visor::host::kKeyUp},
                {.keysym = XK_Down, .code = visor::host::kKeyDown},
                {.keysym = XK_Left, .code = visor::host::kKeyLeft},
                {.keysym = XK_Right, .code = visor::host::kKeyRight},
            },
        };
        for (const ArrowWord& arrow : arrows) {
            const auto down = visor::host::x11::translate_key(arrow.keysym, true);
            VISOR_CHECK(down.has_value());
            if (down.has_value()) {
                VISOR_CHECK(std::holds_alternative<KeyDown>(*down));
                VISOR_CHECK_EQ(std::get<KeyDown>(*down).code, arrow.code);
            }
            const auto up = visor::host::x11::translate_key(arrow.keysym, false);
            VISOR_CHECK(up.has_value());
            if (up.has_value()) {
                VISOR_CHECK(std::holds_alternative<KeyUp>(*up));
                VISOR_CHECK_EQ(std::get<KeyUp>(*up).code, arrow.code);
            }
        }
    }

    // Words the table does not know drop to nothing: function keys, keypad
    // variants (KP_Enter and KP_Space wait for a later layer), and the void
    // zero.
    {
        VISOR_CHECK(!visor::host::x11::translate_key(XK_F1, true).has_value());
        VISOR_CHECK(!visor::host::x11::translate_key(XK_KP_Enter, true).has_value());
        VISOR_CHECK(!visor::host::x11::translate_key(XK_KP_Space, false).has_value());
        VISOR_CHECK(!visor::host::x11::translate_key(0, true).has_value());
    }

    // Pointer buttons pass their number and position through: a left press
    // at (12, 34), a right release at the origin. The wheel (4 and 5) is a
    // scroll, not a button word -- it drops until a scroll layer exists.
    {
        XButtonEvent word{};
        word.type = ButtonPress;
        word.button = Button1;
        word.x = 12;
        word.y = 34;
        const auto down = visor::host::x11::translate_button(word);
        VISOR_CHECK(down.has_value());
        if (down.has_value()) {
            VISOR_CHECK(std::holds_alternative<PointerDown>(*down));
            VISOR_CHECK_EQ(std::get<PointerDown>(*down).button, 1U);
            VISOR_CHECK_EQ(std::get<PointerDown>(*down).pos.x, 12);
            VISOR_CHECK_EQ(std::get<PointerDown>(*down).pos.y, 34);
        }

        word.type = ButtonRelease;
        word.button = Button3;
        word.x = 0;
        word.y = 0;
        const auto up = visor::host::x11::translate_button(word);
        VISOR_CHECK(up.has_value());
        if (up.has_value()) {
            VISOR_CHECK(std::holds_alternative<PointerUp>(*up));
            VISOR_CHECK_EQ(std::get<PointerUp>(*up).button, 3U);
        }

        word.type = ButtonPress;
        word.button = Button4; // the wheel
        VISOR_CHECK(!visor::host::x11::translate_button(word).has_value());
        word.button = Button5; // and its mate
        VISOR_CHECK(!visor::host::x11::translate_button(word).has_value());
    }

    // Motion passes through as the canvas coordinate: negative included, the
    // pointer can sit above or left of the window and the core clips the
    // word like any other out-of-bounds position.
    {
        XMotionEvent word{};
        word.x = 5;
        word.y = -3;
        const auto move = visor::host::x11::translate_motion(word);
        VISOR_CHECK(std::holds_alternative<PointerMove>(move));
        VISOR_CHECK_EQ(std::get<PointerMove>(move).pos.x, 5);
        VISOR_CHECK_EQ(std::get<PointerMove>(move).pos.y, -3);
    }
}
