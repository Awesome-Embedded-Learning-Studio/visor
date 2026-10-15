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
 * A theme maps the background and ink roles onto canvas states. On means
 * a lit pixel in the X11 and OLED hosts, while other exporters may choose
 * their own polarity. The named presets retain their original bit values.
 * Corner radii keep filled faces and their inner outlines on the same geometry.
 *
 * Widget::set_theme propagates the scheme through the tree and invalidates
 * every widget. Without a theme, shape controls use Off ground and On ink;
 * text controls use their explicit ink. The scheme storage must outlive
 * the tree that refers to it.
 */
struct Theme {
    canvas::Color background;
    canvas::Color foreground;
    CornerRadius corner_radius{};
};

/// The original polarity: Off ground, On ink, square corners.
inline constexpr Theme kLightTheme{
    .background = canvas::Color::Off,
    .foreground = canvas::Color::On,
};

/// The opposite polarity: On ground, Off ink, square corners.
inline constexpr Theme kDarkTheme{
    .background = canvas::Color::On,
    .foreground = canvas::Color::Off,
};

} // namespace visor::ui
