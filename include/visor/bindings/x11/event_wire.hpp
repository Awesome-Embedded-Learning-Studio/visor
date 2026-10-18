#pragma once

#include <cstdint>
#include <optional>

#include <X11/Xlib.h>

#include "visor/bindings/events.hpp"

namespace visor::host::x11 {

/**
 * @brief Translates one keysym-resolved keyboard word into the core's Event,
 *        pure logic.
 *
 * The keysym resolution itself is the X half's job (XLookupString consults
 * the display's keymap, a connection this function stays free of): that half
 * hands over the keysym, whether the word is a press or a release, and the
 * X modifier mask that came with the event. Printable ASCII keysyms ride the
 * code-point convention unchanged, space included (0x20, which is why
 * activation consumers accept kCharSpace beside kKeySpace). The keys the core
 * names map onto their named codes, the arrows taking the out-of-band values
 * from events.hpp. Everything else -- keypad variants, function keys, dead
 * accents -- translates to nothing, the same honest refusal as an empty table
 * entry.
 */
[[nodiscard]] std::optional<host::Event> translate_key(std::uint32_t keysym, bool pressed,
                                                       unsigned int state) noexcept;

/**
 * @brief Translates one X pointer button word. Buttons 1 through 3 pass their
 *        number through and carry the event's modifier mask. Buttons 4 through
 *        7 are wheel words on press: 4 up, 5 down, 6 left, 7 right. Their
 *        releases carry no new word.
 */
[[nodiscard]] std::optional<host::Event> translate_button(const XButtonEvent& event) noexcept;

/**
 * @brief Translates one X pointer motion word. The position passes through in
 *        window coordinates -- the window is the canvas, and a pointer above
 *        or left of it simply carries negative coordinates for the core to
 *        clip like any other out-of-bounds word.
 */
[[nodiscard]] host::Event translate_motion(const XMotionEvent& event) noexcept;

} // namespace visor::host::x11
