#pragma once

#include <cstdint>

#include "visor/canvas/color.hpp"

namespace visor::ui {

/// Per-corner rounding, each radius in pixels. Zero is square. The radius is
/// clamped to half the shorter side at draw time, so an oversized radius
/// collapses into a capsule instead of overflowing.
struct CornerRadius {
    std::uint8_t top_left = 0;
    std::uint8_t top_right = 0;
    std::uint8_t bottom_right = 0;
    std::uint8_t bottom_left = 0;
};

/**
 * @brief The semantic colors of one interface scheme, mapped onto the two
 *        states a mono canvas has.
 *
 * A theme in a 1bpp world is not a palette, it is a polarity: which canvas
 * state plays the background and which plays the ink. The light scheme is
 * dark ink on a lit panel's complement (Off background, On foreground -- the
 * colors every control hardcoded before themes arrived), the dark scheme
 * flips both. The corner radius rides along because in the predecessor it
 * was a scheme decision too (per-corner rounding traveled with the theme),
 * and because mono controls have no other geometry knobs worth carrying.
 *
 * Controls consult the theme through the tree's wiring (Widget::set_theme);
 * an unwired tree keeps the pre-theme hardcoded polarities, so existing
 * judgments hold without a theme in sight. Switching at runtime is one
 * set_theme call: the walk re-reports every widget's bounds, the repaint
 * follows.
 */
struct Theme {
    canvas::Color background;
    canvas::Color foreground;
    CornerRadius corner_radius{};
};

/// Dark ink on a lit complement: the polarity every control hardcoded
/// before themes arrived (Off ground, On ink, square corners).
inline constexpr Theme kLightTheme{
    .background = canvas::Color::Off,
    .foreground = canvas::Color::On,
};

/// The flip: lit ink on a dark complement (On ground, Off ink).
inline constexpr Theme kDarkTheme{
    .background = canvas::Color::On,
    .foreground = canvas::Color::Off,
};

} // namespace visor::ui
