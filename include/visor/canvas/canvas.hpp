#pragma once
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_traits.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace visor::canvas {

/**
 * @brief A fixed-capacity drawing surface in one pixel format, sized at compile time.
 *
 * The format is the second parameter and defaults to Mono1, so every call site
 * written before the color chapter -- Canvas<Size> -- still names the mono
 * canvas, and only a site that wants color spells the format out. The format
 * arrives as a traits struct (pixel type, row size, Ink mapping, pixel verbs),
 * so the canvas itself never knows a format by name: it adds up the bytes and
 * hands each pixel verb to the traits.
 *
 * The rows are the canvas-internal packing, not any display hardware's page
 * format. Mono1 keeps its MSB-first bit packing, (Width+7)/8 bytes per row with
 * zero padding tails. RGB565 and XRGB8888 rows are exact, two and four bytes
 * per pixel.
 *
 * The Size/Format pair is the compile-time raster profile: a full frame, a
 * horizontal strip and the swap buffers of SwapCanvas below are this one
 * family in different configurations, never separate types. A build shops the
 * profile by buffer budget -- kBufferBytes is the number it shops with --
 * while the screen size stays a runtime loop parameter that only says which
 * windows to render this round.
 *
 * Pixel access is bounded: set reports an out-of-bounds coordinate with false
 * and touches nothing, get reads out of bounds as the format's zero (unlit for
 * Mono1, black for the color formats). Nothing is checked twice further down --
 * the pen's stance is that strokes never check bounds, so this is the last
 * place that can.
 */
template <base::SizeI CanvasSize, typename Format = Mono1Traits>
class Canvas {
  public:
    using Traits = Format;
    using Pixel = Format::Pixel;

    static constexpr std::size_t kWidth = CanvasSize.w;
    static constexpr std::size_t kHeight = CanvasSize.h;
    static constexpr std::size_t kStrideBytes = Format::bytes_per_row(kWidth);
    /// The whole buffer in bytes: the budget a raster profile costs.
    static constexpr std::size_t kBufferBytes = kStrideBytes * kHeight;

    /// Fills every pixel with one value, the whole surface in the format's ink.
    constexpr void fill(Pixel value) noexcept {
        Format::fill(bits_.data(), kWidth, kHeight, value);
    }

    /// The two-state fill: every pixel lit or unlit. Lit means the format's
    /// white, unlit its black, so a mono canvas keeps the exact behavior
    /// set_clear had while a color canvas paints a white or black ground.
    constexpr void set_clear(bool lit) noexcept {
        fill(Format::from_ink(lit ? Ink::white() : Ink::black()));
    }

    /// Writes one pixel. Out-of-bounds coordinates return false and touch nothing.
    [[nodiscard]] constexpr bool set(base::PointI p, Pixel value) noexcept {
        if (p.x < 0 || p.y < 0) {
            return false;
        }
        const auto x = static_cast<std::size_t>(p.x);
        const auto y = static_cast<std::size_t>(p.y);
        if (x >= kWidth || y >= kHeight) {
            return false;
        }
        Format::store(bits_.data() + (y * kStrideBytes), x, value);
        return true;
    }

    /// Reads one pixel. Out-of-bounds coordinates read as the format's zero.
    [[nodiscard]] constexpr Pixel get(base::PointI p) const noexcept {
        if (p.x < 0 || p.y < 0) {
            return Pixel{};
        }
        const auto x = static_cast<std::size_t>(p.x);
        const auto y = static_cast<std::size_t>(p.y);
        if (x >= kWidth || y >= kHeight) {
            return Pixel{};
        }
        return Format::load(bits_.data() + (y * kStrideBytes), x);
    }

    /// Raw rows for the host table's flush, kStrideBytes bytes per row.
    [[nodiscard]] constexpr std::span<const std::uint8_t> pixels() const noexcept { return bits_; }
    [[nodiscard]] constexpr std::span<std::uint8_t> pixels() noexcept { return bits_; }

  private:
    std::array<std::uint8_t, kStrideBytes * kHeight> bits_{};
};

/**
 * @brief The third raster profile configuration: two stores that trade
 *        places between rounds, the swap buffers of the architecture
 *        sentence made one template instead of a promise.
 *
 * The vocabulary is Canvas's own -- same Pixel, same Traits, same row size,
 * same bounded set and out-of-bounds-reads-zero get -- so a run loop, a
 * painter and a host table take a SwapCanvas anywhere they took a Canvas,
 * which is what "one model, different configurations" has to mean. The
 * drawing verbs and pixels() address the back store; swap() exchanges the
 * stores after a round, so the next round renders into the other one while
 * the front holds exactly what the previous round painted. A host that
 * scans out instead of copying at flush reads front_pixels() at its own
 * pace: the front is always one complete round old, never a partial frame,
 * because visor's buffers store exactly the window being rendered -- there
 * is no partial update of a big buffer to grow stale in the first place,
 * and therefore no software blit for a double buffer to owe.
 */
template <base::SizeI CanvasSize, typename Format = Mono1Traits>
class SwapCanvas {
  public:
    using Traits = Format;
    using Pixel = Format::Pixel;

    static constexpr std::size_t kWidth = CanvasSize.w;
    static constexpr std::size_t kHeight = CanvasSize.h;
    static constexpr std::size_t kStrideBytes = Canvas<CanvasSize, Format>::kStrideBytes;

    constexpr void fill(Pixel value) noexcept { back_.fill(value); }
    constexpr void set_clear(bool lit) noexcept { back_.set_clear(lit); }
    /// Writes one pixel into the back store, the canvas's own bounds line.
    [[nodiscard]] constexpr bool set(base::PointI p, Pixel value) noexcept {
        return back_.set(p, value);
    }
    /// Reads one pixel from the back store. Out-of-bounds reads the format's
    /// zero, same as the canvas.
    [[nodiscard]] constexpr Pixel get(base::PointI p) const noexcept { return back_.get(p); }

    /// The back store's raw rows, what a submission hands the host table.
    [[nodiscard]] constexpr std::span<const std::uint8_t> pixels() const noexcept {
        return back_.pixels();
    }
    [[nodiscard]] constexpr std::span<std::uint8_t> pixels() noexcept { return back_.pixels(); }

    /// The front store's raw rows: what the host last received, for a host
    /// or a judgment that reads while the next round renders.
    [[nodiscard]] constexpr std::span<const std::uint8_t> front_pixels() const noexcept {
        return front_.pixels();
    }

    /// Exchanges the stores after a round. The next round draws into the
    /// other store; the front now holds the round just submitted.
    constexpr void swap() noexcept { std::swap(back_, front_); }

  private:
    Canvas<CanvasSize, Format> front_{};
    Canvas<CanvasSize, Format> back_{};
};
} // namespace visor::canvas
