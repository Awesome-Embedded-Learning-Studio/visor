#pragma once

#include <cstdint>
#include <optional>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"

namespace visor::host::wayland {

/// The pointer buttons the protocol carries. Wayland passes the kernel's own
/// BTN_* words through untouched, so the values are evdev's: 0x110 left,
/// 0x111 right, 0x112 middle (linux/input-event-codes.h, the one home of that
/// truth).
inline constexpr std::uint32_t kButtonLeft = 0x110;
inline constexpr std::uint32_t kButtonRight = 0x111;
inline constexpr std::uint32_t kButtonMiddle = 0x112;

/// The modifier-mask bits a wl_keyboard modifiers word carries under the
/// standard evdev keymap: Shift 1<<0, Control 1<<2, Alt (Mod1) 1<<3, Super
/// (Mod4) 1<<6. The lock and numeric modifiers stay untranslated -- the core
/// names four flags, and those carry none of them.
inline constexpr std::uint32_t kModShift = 0x01;
inline constexpr std::uint32_t kModControl = 0x04;
inline constexpr std::uint32_t kModAlt = 0x08;
inline constexpr std::uint32_t kModSuper = 0x40;

/**
 * @brief Translates one xkb modifier mask into the core's named flags, pure
 *        logic.
 */
[[nodiscard]] host::Modifiers translate_modifiers(std::uint32_t xkb_mask) noexcept;

/**
 * @brief Translates one wl_keyboard key word into the core's Event, pure
 *        logic.
 *
 * The protocol hands over the kernel's own key code (linux/input-event-codes.h
 * numbering, with none of X's +8). That is the evdev convention the core
 * already speaks, so the whole evdev line applies: Backspace, Tab, Enter and
 * Space ride their evdev values unchanged, and only the arrows move, onto the
 * core's out-of-band values from events.hpp -- evdev's 103 to 108 sit inside
 * printable ASCII, the collision B16 settled for every host. Every other code
 * passes through untouched, modifier keys included; the core compares codes
 * and never decodes them. This layer produces no text: letters and digits stay
 * key codes, and a text control sees the space key as 57, the same honest
 * refusal as the evdev translator.
 */
[[nodiscard]] std::optional<host::Event> translate_key(std::uint32_t code, bool pressed,
                                                       host::Modifiers modifiers) noexcept;

/**
 * @brief Translates one wl_pointer button word. The three known buttons take
 *        the evdev translator's own numbering (1 left, 2 right, 3 middle --
 *        the kernel code's order, so the same physical button carries the same
 *        number under both hosts) at the tracked pointer position. Any other
 *        button translates to nothing, the same honest refusal as an empty
 *        table entry.
 */
[[nodiscard]] std::optional<host::Event> translate_button(std::uint32_t button, bool pressed,
                                                          base::PointI pos,
                                                          host::Modifiers modifiers) noexcept;

/**
 * @brief Translates one pointer position word. The position passes through in
 *        surface coordinates -- the surface is the canvas, and a pointer above
 *        or left of it simply carries negative coordinates for the core to
 *        clip like any other out-of-bounds word.
 */
[[nodiscard]] host::Event translate_motion(base::PointI pos) noexcept;

/**
 * @brief Translates one wheel step pair read off the axis words, pure logic.
 *
 * The protocol measures an axis step as a vector in the pointer's own
 * coordinate space, so a positive vertical step points down the screen -- the
 * same direction the core calls "toward the user". Both values pass through
 * unchanged, then, and a step pair with nothing in it reads as no word at all.
 */
[[nodiscard]] std::optional<host::Event> translate_wheel(int horizontal_steps, int vertical_steps,
                                                         base::PointI pos,
                                                         host::Modifiers modifiers) noexcept;

} // namespace visor::host::wayland
