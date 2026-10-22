#pragma once

namespace visor::host {

/// @brief The keyboard modifiers held while a word was produced. Four named
///        flags, not a bit mask: the event table names them one by one and a
///        reader should not have to decode a number to see what is held.
///
///        Born on the key words, carried by the pointer's press and wheel
///        too, so it lives in its own header between the two families.
struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

} // namespace visor::host
