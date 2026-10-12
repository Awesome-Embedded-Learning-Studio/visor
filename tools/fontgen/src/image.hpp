#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace visor::fontgen {

/**
 * @brief A 1bpp image in the canvas packing: row-major, MSB-first, one bit per
 *        pixel, each row padded to a whole byte with zero bits.
 *
 * This is visor::canvas::Canvas's internal format, not any display hardware's
 * page format. Keeping the tool's in-memory shape identical to the core's
 * means a generated array can be consumed as canvas rows without repacking.
 */
struct MonoImage {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> rows;

    /// Bytes per row in the packed layout: (width + 7) / 8.
    [[nodiscard]] std::size_t stride() const noexcept { return (width + 7) / 8; }
};

/**
 * @brief   Allocates a zero-filled image of the given size.
 * @note    The tool runs with exceptions off, so a failed allocation terminates
 *          the process instead of throwing. Generation runs on the development
 *          PC where running out of memory is not a recoverable condition.
 */
[[nodiscard]] MonoImage make_mono_image(std::size_t width, std::size_t height);

} // namespace visor::fontgen
