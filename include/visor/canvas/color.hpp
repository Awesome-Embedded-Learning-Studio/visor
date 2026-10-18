#pragma once

#include <cstddef>
#include <cstdint>

namespace visor::canvas {

/**
 * @brief A concrete color value: one alpha byte and three color bytes.
 *
 * The value is 0xAARRGGBB, the same word an XRGB8888 target stores once the
 * alpha byte is read as "ignore". Mono and RGB565 targets map it down to their
 * own pixels through their traits; the theme chooses values, the canvas stores
 * pixels. Alpha is only an opacity flag for now; blending is a later layer.
 */
struct Ink {
    std::uint32_t argb = 0;

    [[nodiscard]] friend constexpr bool operator==(Ink lhs, Ink rhs) noexcept {
        return lhs.argb == rhs.argb;
    }
    [[nodiscard]] friend constexpr bool operator!=(Ink lhs, Ink rhs) noexcept {
        return !(lhs == rhs);
    }

    [[nodiscard]] static constexpr Ink black() noexcept { return Ink{0xff000000}; }
    [[nodiscard]] static constexpr Ink white() noexcept { return Ink{0xffffffff}; }

    /// Opaque ink from a 0xRRGGBB word; the alpha byte is filled in.
    [[nodiscard]] static constexpr Ink from_rgb(std::uint32_t rgb) noexcept {
        return Ink{0xff000000U | (rgb & 0x00ffffffU)};
    }
};

/**
 * @brief The semantic color slots a control names.
 *
 * Controls say which slot a stroke belongs to, never which color it is: a
 * label names Foreground, a pressed button names Accent, and the theme decides
 * what those names look like on the target. The paint layer resolves the slot
 * once per stroke, so no control carries a raw color again.
 *
 * Accent and OnAccent are a pair: OnAccent contrasts with Accent, so a filled
 * face and its label can be chosen together. Muted and Disabled are the
 * secondary and inactive slots. Border is the outline slot.
 */
enum class ColorRole : std::uint8_t {
    Background,
    Foreground,
    Muted,
    Accent,
    OnAccent,
    Border,
    Disabled,
};

/// The number of slots in ColorRole; the paint layer's table size.
inline constexpr std::size_t kColorRoleCount = static_cast<std::size_t>(ColorRole::Disabled) + 1;

/// Mono1's interim ink rule: any non-black RGB is ink. The format chapter
/// moves this behind the Mono1 traits; the paint layer asks it once per role.
[[nodiscard]] constexpr bool mono_ink(Ink ink) noexcept {
    return (ink.argb & 0x00ffffffU) != 0;
}

} // namespace visor::canvas
