#pragma once

#include <cstdint>

#include "visor/bindings/events/modifiers.hpp"

namespace visor::host {

/// @brief A key went down. code is host-defined: a PC host maps its platform's
///        key symbols, a board host maps its matrix scan lines. The core only
///        ever compares codes, it never decodes them.
struct KeyDown {
    std::uint32_t code;
    Modifiers modifiers{};
};

/// @brief A key came back up, closing the KeyDown that carried the same code.
struct KeyUp {
    std::uint32_t code;
    Modifiers modifiers{};
};

/// @brief The key codes the core names itself. Tab is the tree's own traversal
///        semantic, so the dispatcher must recognize it. Enter and Space are
///        activation, the keyboard's press, named so a control can recognize
///        them without host knowledge. Backspace and the four arrows are the
///        editing keys, named for the same reason (B16): a text control moves
///        its caret and deletes with them, without guessing at host codes.
///        Every other code stays host-defined and opaque: controls may
///        receive it and ignore it, never decode it.
///
///        Backspace, Tab and Enter follow the evdev table so a Linux host
///        passes them through untouched. The arrows deliberately do not:
///        evdev's arrow numbers (103 through 108) sit inside printable
///        ASCII, colliding head-on with the text convention (a host that
///        produces text maps printable keys to code points -- under evdev
///        numbering, 'i' and Left are the same wire value). The arrows take
///        values outside the printable band instead, and every host maps its
///        own arrows onto them, the same as any non-Linux key. Space keeps
///        its evdev value for activation consumers; a text-producing host
///        sends the space character as its code point (32), where '9' and
///        kKeySpace share 57 by the convention and each consumer reads its
///        own meaning.
inline constexpr std::uint32_t kKeyBackspace = 14;
inline constexpr std::uint32_t kKeyTab = 15;
inline constexpr std::uint32_t kKeyEnter = 28;
inline constexpr std::uint32_t kKeySpace = 57;
inline constexpr std::uint32_t kKeyUp = 0x110;
inline constexpr std::uint32_t kKeyLeft = 0x112;
inline constexpr std::uint32_t kKeyRight = 0x113;
inline constexpr std::uint32_t kKeyDown = 0x111;

/// @brief The space character as its code point, the number a text-producing
///        host actually types for the space bar. Activation consumers accept
///        it beside kKeySpace so the keyboard's press reads the same under an
///        evdev host (57) and a text host (0x20): the two numbers share the
///        wire by the convention above, and each consumer reads its own.
inline constexpr std::uint32_t kCharSpace = 0x20;

} // namespace visor::host
