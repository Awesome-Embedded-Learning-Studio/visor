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
 * so a polarity switch still flips every pixel. kColorTheme is the color
 * preset: the same seven roles with real values, which a mono target folds
 * back down through its own rule (every non-black RGB is ink), so a color
 * theme on a mono canvas keeps the light-ink-on-dark-ground polarity and
 * loses the grays between the roles.
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

/**
 * @brief The color preset: real values for the seven roles.
 *
 * The ground is pure black on purpose. Mono1 reads exactly one value as unlit,
 * and that value is black, so a color theme painted on a mono target keeps
 * unlit ground and lit ink instead of inverting the picture. The accent is an
 * iris blue picked to stay readable against both the ground and OnAccent; the
 * muted, border and disabled roles are three steps of the same blue-gray so a
 * disabled control reads as one without changing its shape.
 *
 * These values are the author's to tune: every control names roles, so the
 * whole interface recolors from this table alone.
 */
inline constexpr Theme kColorTheme{
    .background = canvas::Ink::black(),
    .foreground = canvas::Ink::from_rgb(0x00e7ebf5),
    .muted = canvas::Ink::from_rgb(0x008a93a6),
    .accent = canvas::Ink::from_rgb(0x004a62c8),
    .on_accent = canvas::Ink::from_rgb(0x00f2f5ff),
    .border = canvas::Ink::from_rgb(0x0039435c),
    .disabled = canvas::Ink::from_rgb(0x00555d70),
};

} // namespace visor::ui
