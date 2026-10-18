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
 * @brief One interface scheme: every semantic role mapped to a concrete ink.
 *
 * The theme is where color choices live. Controls name roles, the paint layer
 * asks this table what a role looks like, and a target's traits turn that ink
 * into its own pixels. The light and dark presets are exact inverses in mono,
 * so a polarity switch still flips every pixel.
 *
 * Corner radii keep filled faces and their inner outlines on the same geometry.
 * Widget::set_theme propagates the scheme through the tree and invalidates every
 * widget; without a theme the light preset is the default. The scheme storage
 * must outlive the tree that refers to it.
 */
struct Theme {
    canvas::Ink background = canvas::Ink::black();
    canvas::Ink foreground = canvas::Ink::white();
    canvas::Ink muted = canvas::Ink::white();
    canvas::Ink accent = canvas::Ink::white();
    canvas::Ink on_accent = canvas::Ink::black();
    canvas::Ink border = canvas::Ink::white();
    canvas::Ink disabled = canvas::Ink::white();
    CornerRadius corner_radius{};

    /// The ink a role names. The switch is exhaustive on purpose: adding a
    /// role without teaching the themes about it should be a compiler warning,
    /// not a silent fallback.
    [[nodiscard]] constexpr canvas::Ink ink(canvas::ColorRole role) const noexcept {
        switch (role) {
            case canvas::ColorRole::Background:
                return background;
            case canvas::ColorRole::Foreground:
                return foreground;
            case canvas::ColorRole::Muted:
                return muted;
            case canvas::ColorRole::Accent:
                return accent;
            case canvas::ColorRole::OnAccent:
                return on_accent;
            case canvas::ColorRole::Border:
                return border;
            case canvas::ColorRole::Disabled:
                return disabled;
        }
        return foreground;
    }
};

/// The original polarity: unlit ground, lit ink, square corners.
inline constexpr Theme kLightTheme{};

/// The opposite polarity: lit ground, unlit ink, square corners.
inline constexpr Theme kDarkTheme{
    .background = canvas::Ink::white(),
    .foreground = canvas::Ink::black(),
    .muted = canvas::Ink::black(),
    .accent = canvas::Ink::black(),
    .on_accent = canvas::Ink::white(),
    .border = canvas::Ink::black(),
    .disabled = canvas::Ink::black(),
};

} // namespace visor::ui
