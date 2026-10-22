#pragma once

#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events/modifiers.hpp"

namespace visor::host {

/// @brief The pointer moved. pos is in canvas coordinates.
struct PointerMove {
    base::PointI pos;
};

/// @brief A pointer button went down at pos. button is host-defined and small.
struct PointerDown {
    std::uint32_t button;
    base::PointI pos;
    Modifiers modifiers{};
};

/// @brief A pointer button came back up at pos, closing the PointerDown that
///        carried the same button.
struct PointerUp {
    std::uint32_t button;
    base::PointI pos;
};

/// @brief A wheel turned at pos. dx and dy are detents, not pixels and not
///        lines: one physical notch is one unit. dy is positive when the
///        wheel turns toward the user (content moves down), dx is positive to
///        the right. A value of two is two notches in one word, not two words.
struct PointerWheel {
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    base::PointI pos;
    Modifiers modifiers{};
};

} // namespace visor::host
