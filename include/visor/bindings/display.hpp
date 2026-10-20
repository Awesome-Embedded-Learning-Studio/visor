#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/pixel_traits.hpp"

namespace visor::host {

/**
 * @brief Pixel layouts a FlushRegion can carry.
 *
 * Mono1 is the bit-packed format the canvas started with: one bit per pixel,
 * MSB first, (width+7)/8 bytes per row. RGB565 is two bytes per pixel, low
 * byte first, and XRGB8888 is four, alpha byte most significant. The host
 * table carries no knowledge of any of them: the enum travels with the region
 * and the host that speaks a format is the host that accepts it.
 */
enum class PixelFormat : std::uint8_t { Mono1, RGB565, XRGB8888 };

/// Bytes one row of this format takes at the given width; Mono1 packs bits, so
/// its answer is not a multiple of the width.
[[nodiscard]] constexpr std::size_t row_bytes(PixelFormat format, std::size_t width) noexcept {
    switch (format) {
        case PixelFormat::Mono1:
            return (width + 7) / 8;
        case PixelFormat::RGB565:
            return width * 2;
        case PixelFormat::XRGB8888:
            return width * 4;
    }
    return (width + 7) / 8;
}

/**
 * @brief The wire format a canvas format submits as.
 *
 * The bridge between the two vocabularies lives on the host side on purpose:
 * a canvas names its own pixel type and stays ignorant of the wire, while the
 * host names the byte layout crossing the boundary. This table is the whole
 * relation between them, one row per format, so a new format that forgets to
 * say what it puts on the wire fails to compile at its first flush.
 */
template <typename Traits>
struct WireFormat;

template <>
struct WireFormat<canvas::Mono1Traits> {
    static constexpr PixelFormat kValue = PixelFormat::Mono1;
};

template <>
struct WireFormat<canvas::Rgb565Traits> {
    static constexpr PixelFormat kValue = PixelFormat::RGB565;
};

template <>
struct WireFormat<canvas::Xrgb8888Traits> {
    static constexpr PixelFormat kValue = PixelFormat::XRGB8888;
};

template <typename Traits>
inline constexpr PixelFormat kWireFormat = WireFormat<Traits>::kValue;

/// @brief The display surface a host presents to the core: its logical size
///        and the format its flush regions carry. A zero-sized screen means
///        the host is not ready; a desktop refuses to lay out against it.
struct HostInfo {
    base::SizeI screen{};
    PixelFormat format = PixelFormat::Mono1;
};

/**
 * @brief One submission of pixels travelling from a canvas to a host.
 *
 * rect is half-open in canvas coordinates, pixels points at the first byte of
 * rect's top row, and stride_bytes is the hop from one row down to the next.
 * The mono layout matches the canvas packing: row-major, MSB first,
 * (width+7)/8 bytes per row.
 */
struct FlushRegion {
    base::RectI rect;
    const std::uint8_t* pixels = nullptr;
    std::size_t stride_bytes = 0;
    PixelFormat format = PixelFormat::Mono1;
};

} // namespace visor::host
