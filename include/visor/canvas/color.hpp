#pragma once

#include <cstdint>

namespace visor::canvas {
/**
 * @brief Ink of the mono profile: one bit per pixel, lit or unlit.
 *
 * The values speak about the bit, not a physical color: which shade a set bit
 * shows is the panel's business. Later profiles define their own Color and
 * keep the name, so profile-generic code survives the RGB565 chapter unchanged.
 */
enum class Color : std::uint8_t { Off = 0, On = 1 };

} // namespace visor::canvas
