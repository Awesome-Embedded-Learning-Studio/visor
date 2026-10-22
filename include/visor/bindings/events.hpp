#pragma once

#include <type_traits>
#include <variant>

#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace visor::host {

/**
 * @brief One input fact travelling from the host into the core, carried as a
 *        variant over the six ch2 events. Gestures, text input and key
 *        repeat are later layers grown from these six, not new table entries
 *        -- text input in particular is a KeyDown convention: a host that
 *        produces text maps each printable key to its code point, and the
 *        core's named codes carry the editing semantics around it.
 *
 *        The words themselves live in their family headers under events/ --
 *        key_words.hpp and pointer_words.hpp, with modifiers.hpp between
 *        them -- and this umbrella carries only the variant that ties the
 *        six into one queueable word. Existing includes of events.hpp keep
 *        working untouched; a consumer that wants one family takes its own
 *        header.
 */
using Event = std::variant<KeyDown, KeyUp, PointerMove, PointerDown, PointerUp, PointerWheel>;

// The ch2 event queue stores plain values only, so this is a promise about the
// vocabulary, checked where the promise is made.
static_assert(std::is_trivially_copyable_v<Event>);

} // namespace visor::host
