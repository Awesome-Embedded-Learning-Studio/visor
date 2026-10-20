#pragma once

#include <cstdint>

namespace visor::canvas {

/**
 * @brief RGB565 to XRGB8888: widen each channel by repeating its top bits.
 *
 * Bit replication, not a plain shift: five bits become eight by repeating the
 * top three, six become eight by repeating the top two, which lands white on
 * 0xFF rather than 0xF8 and keeps a ramp of grays monotone. The conversion is
 * exact in the narrow direction, so widening and narrowing again returns the
 * original pixel for every one of the 65536 values -- the round trip the
 * judgments assert.
 */
// Both directions are word arithmetic on unsigned channels; the shift counts are
// plain ints, which the signed-bitwise check reads as signed operands.
// NOLINTBEGIN(bugprone-signed-bitwise)
[[nodiscard]] constexpr std::uint32_t rgb565_to_xrgb8888(std::uint16_t pixel) noexcept {
    const std::uint32_t red = (pixel >> 11) & 0x1fU;
    const std::uint32_t green = (pixel >> 5) & 0x3fU;
    const std::uint32_t blue = pixel & 0x1fU;
    const std::uint32_t red8 = (red << 3) | (red >> 2);
    const std::uint32_t green8 = (green << 2) | (green >> 4);
    const std::uint32_t blue8 = (blue << 3) | (blue >> 2);
    return 0xff000000U | (red8 << 16) | (green8 << 8) | blue8;
}

/**
 * @brief XRGB8888 to RGB565: keep the top bits of each channel, drop the rest.
 *
 * The alpha byte is ignored, the same reading Xrgb8888Traits::from_ink makes.
 * A Wayland server receiving a 32-bit client buffer needs this: the client
 * spoke 8888, the panel or the compositor's own surface speaks 565.
 */
[[nodiscard]] constexpr std::uint16_t xrgb8888_to_rgb565(std::uint32_t pixel) noexcept {
    const std::uint32_t red = (pixel >> 16) & 0xffU;
    const std::uint32_t green = (pixel >> 8) & 0xffU;
    const std::uint32_t blue = pixel & 0xffU;
    return static_cast<std::uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}
// NOLINTEND(bugprone-signed-bitwise)

} // namespace visor::canvas
