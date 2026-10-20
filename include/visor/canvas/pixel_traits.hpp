#pragma once

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/canvas/color.hpp"

#include <cstddef>
#include <cstdint>

namespace visor::canvas {

// Every channel operation in this header is unsigned word arithmetic; the shift
// counts and channel bounds are plain ints, which the signed-bitwise check reads
// as signed operands. The masks stay explicit here because this is the one place
// that knows each format's bit layout.
// NOLINTBEGIN(bugprone-signed-bitwise)

/// @brief Pixel x's bit position inside its byte, MSB first: pixel 0 rides the top
///        bit. The one home of the mono packing formula; hosts decode with it too.
constexpr std::uint8_t pixel_bit_position(std::size_t x) noexcept {
    return static_cast<std::uint8_t>(7 - (x % 8));
}

/**
 * @brief Map from Ink down to this format's pixel value.
 *
 * Mono1 is the format the canvas started as: one bit per pixel, packed MSB
 * first, (width+7)/8 bytes per row. The pixel type is bool because a lit pixel
 * has exactly two states and no intensity; the packing stays where it was, so
 * a mono canvas keeps its memory shape byte for byte.
 *
 * Every format in visor is a struct under this shape: a Pixel type, the row
 * size in bytes, the Ink mapping, and the two pixel verbs. A canvas and a
 * painter are parameterized on the struct, never on a format name, so adding
 * a format is adding a struct and nothing else. All of it is constexpr and
 * lives in headers: the traits carry no storage.
 */
struct Mono1Traits {
    using Pixel = bool;

    /// Bytes per row: the packed width, tail byte included.
    [[nodiscard]] static constexpr std::size_t bytes_per_row(std::size_t width) noexcept {
        return (width + 7) / 8;
    }

    /// Ink to pixel: any non-black RGB is ink. The rule the mono themes rely on,
    /// and the reason a color theme on a mono target has no grays: Muted and
    /// Disabled land lit like everything else that is not pure black.
    [[nodiscard]] static constexpr Pixel from_ink(Ink ink) noexcept {
        return (ink.argb & 0x00ffffffU) != 0;
    }

    /// Writes one pixel into a row that starts at this row's first byte.
    static constexpr void store(std::uint8_t* row, std::size_t x, Pixel value) noexcept {
        base::bit::BitMask<std::uint8_t> word{row[x / 8]};
        word.deposit(base::bit::bit_at(pixel_bit_position(x)), value ? 1 : 0);
        row[x / 8] = word.raw;
    }

    /// Reads one pixel out of a row that starts at this row's first byte.
    [[nodiscard]] static constexpr Pixel load(const std::uint8_t* row, std::size_t x) noexcept {
        const base::bit::BitMask<std::uint8_t> word{row[x / 8]};
        return word.has(base::bit::mask_bit<std::uint8_t>(pixel_bit_position(x)));
    }

    /// Fills every pixel; the padding bits past the last pixel stay zero.
    static constexpr void fill(std::uint8_t* rows, std::size_t width, std::size_t height,
                               Pixel value) noexcept {
        const std::size_t stride = bytes_per_row(width);
        const std::uint8_t lit_row = value ? base::bit::ones<std::uint8_t>(8) : std::uint8_t{0};
        const base::bit::BitRange padding{
            .low = 0,
            .width = static_cast<std::uint8_t>(8 - (width % 8)),
        };
        for (std::size_t y = 0; y < height; ++y) {
            std::uint8_t* row = rows + (y * stride);
            for (std::size_t byte = 0; byte < stride; ++byte) {
                row[byte] = lit_row;
            }
            if (width % 8 != 0) {
                base::bit::BitMask<std::uint8_t> tail{row[stride - 1]};
                tail.deposit(padding, 0);
                row[stride - 1] = tail.raw;
            }
        }
    }
};

/**
 * @brief RGB565: one 16-bit pixel, five red bits, six green, five blue.
 *
 * The format small color panels speak: two bytes per pixel, no alpha, the
 * green channel one bit longer because the eye is. Bytes go out little endian
 * -- low byte first -- which is the order the panels and the desktop's 16bpp
 * words both expect.
 *
 * A color theme's grays survive here that Mono1 flattens: Muted and Disabled
 * land as muted and disabled pixels, because the format has the room.
 */
struct Rgb565Traits {
    using Pixel = std::uint16_t;

    /// Bytes per row: two per pixel.
    [[nodiscard]] static constexpr std::size_t bytes_per_row(std::size_t width) noexcept {
        return width * 2;
    }

    // The channel masks below are word arithmetic on unsigned values; the shift
    // counts are plain ints, which the signed-bitwise check reads as operands.
    /// Ink to pixel: keep the top bits of each channel, drop alpha and low bits.
    [[nodiscard]] static constexpr Pixel from_ink(Ink ink) noexcept {
        const std::uint32_t red = (ink.argb >> 16) & 0xffU;
        const std::uint32_t green = (ink.argb >> 8) & 0xffU;
        const std::uint32_t blue = ink.argb & 0xffU;
        return static_cast<Pixel>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
    }

    /// Writes one pixel, low byte first.
    static constexpr void store(std::uint8_t* row, std::size_t x, Pixel value) noexcept {
        row[x * 2] = static_cast<std::uint8_t>(value & 0xffU);
        row[(x * 2) + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    /// Reads one pixel, low byte first.
    [[nodiscard]] static constexpr Pixel load(const std::uint8_t* row, std::size_t x) noexcept {
        return static_cast<Pixel>(row[x * 2] | (static_cast<std::uint16_t>(row[(x * 2) + 1]) << 8));
    }

    /// Fills every pixel; no padding exists, the stride is exact.
    static constexpr void fill(std::uint8_t* rows, std::size_t width, std::size_t height,
                               Pixel value) noexcept {
        const std::size_t stride = bytes_per_row(width);
        for (std::size_t y = 0; y < height; ++y) {
            std::uint8_t* row = rows + (y * stride);
            for (std::size_t x = 0; x < width; ++x) {
                store(row, x, value);
            }
        }
    }
};

/**
 * @brief XRGB8888: one 32-bit pixel, alpha byte forced opaque.
 *
 * The desktop's shape: the same 0xAARRGGBB word Ink carries, with the alpha
 * byte read as "ignore". A window server and a PC frame both take this, so it
 * is the format the two ends of a desktop path already speak.
 */
struct Xrgb8888Traits {
    using Pixel = std::uint32_t;

    /// Bytes per row: four per pixel.
    [[nodiscard]] static constexpr std::size_t bytes_per_row(std::size_t width) noexcept {
        return width * 4;
    }

    /// Ink to pixel: the same word, alpha forced opaque.
    [[nodiscard]] static constexpr Pixel from_ink(Ink ink) noexcept {
        return ink.argb | 0xff000000U;
    }

    /// Writes one pixel, low byte first.
    static constexpr void store(std::uint8_t* row, std::size_t x, Pixel value) noexcept {
        std::uint8_t* pixel = row + (x * 4);
        pixel[0] = static_cast<std::uint8_t>(value & 0xffU);
        pixel[1] = static_cast<std::uint8_t>((value >> 8) & 0xffU);
        pixel[2] = static_cast<std::uint8_t>((value >> 16) & 0xffU);
        pixel[3] = static_cast<std::uint8_t>((value >> 24) & 0xffU);
    }

    /// Reads one pixel, low byte first.
    [[nodiscard]] static constexpr Pixel load(const std::uint8_t* row, std::size_t x) noexcept {
        const std::uint8_t* pixel = row + (x * 4);
        return static_cast<Pixel>(pixel[0]) | (static_cast<Pixel>(pixel[1]) << 8) |
               (static_cast<Pixel>(pixel[2]) << 16) | (static_cast<Pixel>(pixel[3]) << 24);
    }

    /// Fills every pixel; no padding exists, the stride is exact.
    static constexpr void fill(std::uint8_t* rows, std::size_t width, std::size_t height,
                               Pixel value) noexcept {
        const std::size_t stride = bytes_per_row(width);
        for (std::size_t y = 0; y < height; ++y) {
            std::uint8_t* row = rows + (y * stride);
            for (std::size_t x = 0; x < width; ++x) {
                store(row, x, value);
            }
        }
    }
};

// NOLINTEND(bugprone-signed-bitwise)

} // namespace visor::canvas
