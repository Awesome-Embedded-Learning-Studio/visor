#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"

#include <cstddef>
#include <cstdint>

namespace visor::host {

/// @brief Pixel layouts a FlushRegion can carry. Ch1 has exactly one.
enum class PixelFormat : std::uint8_t { Mono1 };

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

/**
 * @brief The one boundary between the core and the world: hosts fill it in, the
 *        core only ever calls through it. Ch2 carries three entries: the
 *        display flush, the event poll, and the millisecond clock.
 * @note  poll_event returns false when no event waits, never blocking; what a
 *        full queue drops is the host's decision, not the core's.
 *        clock_ms returns milliseconds since a host-chosen epoch. The value
 *        may wrap, so durations are computed by unsigned subtraction
 *        (now - then), never by comparing absolute values.
 */
struct HostTable {
    void (*flush)(void* context, const FlushRegion& region) noexcept = nullptr;
    bool (*poll_event)(void* context, Event& out) noexcept = nullptr;
    std::uint64_t (*clock_ms)(void* context) noexcept = nullptr;
    void* context = nullptr;
};

/**
 * @brief Submits one region of a larger picture: rect names where the pixels
 *        live on the screen, pixels points at the first byte of rect's top row
 *        in the source buffer, stride_bytes hops from row to row. The general
 *        form of submit_frame; a full frame is the region that covers the
 *        whole canvas, strips and partial repaints are the smaller ones.
 *        An empty table refuses without a crash.
 */
[[nodiscard]] bool submit_region(const HostTable& host, base::RectI rect,
                                 const std::uint8_t* pixels, std::size_t stride_bytes) noexcept;

/**
 * @brief Hands a whole canvas to the host. An empty table returns false, never
 *        crashes. Templates on the canvas type, same lesson as Pen.
 *        Requirement on CanvasT: kWidth, kHeight, kStrideBytes, pixels().
 */
template <typename CanvasT>
[[nodiscard]] bool submit_frame(const HostTable& host, const CanvasT& canvas) noexcept {
    const base::RectI full{
        .pos = base::PointI{0, 0},
        .size =
            base::SizeI{
                .w = static_cast<int>(CanvasT::kWidth),
                .h = static_cast<int>(CanvasT::kHeight),
            },
    };
    return submit_region(host, full, canvas.pixels().data(), CanvasT::kStrideBytes);
}

/**
 * @brief Polls one event through the table. A missing entry and a drained
 *        queue both read as false, so the core survives a host that offers no
 *        events at all, same stance as an empty flush.
 */
[[nodiscard]] bool try_poll_event(const HostTable& host, Event& out) noexcept;

/**
 * @brief Reads the host clock in milliseconds since the host's epoch. A
 *        missing entry reads as zero: time never advances, and no caller dies.
 */
[[nodiscard]] std::uint64_t current_time_ms(const HostTable& host) noexcept;

} // namespace visor::host
