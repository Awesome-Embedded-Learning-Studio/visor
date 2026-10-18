#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"

namespace visor::host {

/// @brief Pixel layouts a FlushRegion can carry. Ch1 has exactly one.
enum class PixelFormat : std::uint8_t { Mono1 };

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
