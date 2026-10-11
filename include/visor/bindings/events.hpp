#pragma once

#include <cstdint>
#include <type_traits>
#include <variant>

#include "visor/base/geometry/point.hpp"

namespace visor::host {

/// @brief A key went down. code is host-defined: a PC host maps its platform's
///        key symbols, a board host maps its matrix scan lines. The core only
///        ever compares codes, it never decodes them.
struct KeyDown {
    std::uint32_t code;
};

/// @brief A key came back up, closing the KeyDown that carried the same code.
struct KeyUp {
    std::uint32_t code;
};

/// @brief The one key code the core names itself. Focus traversal is the
///        tree's own semantic, so the dispatcher has to recognize the
///        traversal key; every other code stays host-defined and opaque. The
///        value is the evdev Tab code, the natural mapping for PC hosts.
inline constexpr std::uint32_t kKeyTab = 15;

/// @brief The pointer moved. pos is in canvas coordinates.
struct PointerMove {
    base::PointI pos;
};

/// @brief A pointer button went down at pos. button is host-defined and small.
struct PointerDown {
    std::uint32_t button;
    base::PointI pos;
};

/// @brief A pointer button came back up at pos, closing the PointerDown that
///        carried the same button.
struct PointerUp {
    std::uint32_t button;
    base::PointI pos;
};

/**
 * @brief One input fact travelling from the host into the core, carried as a
 *        variant over the five ch2 events. Gestures, text input and key
 *        repeat are later layers grown from these five, not new table entries.
 */
using Event = std::variant<KeyDown, KeyUp, PointerMove, PointerDown, PointerUp>;

// The ch2 event queue stores plain values only, so this is a promise about the
// vocabulary, checked where the promise is made.
static_assert(std::is_trivially_copyable_v<Event>);

} // namespace visor::host
