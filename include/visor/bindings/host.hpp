#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/color.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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
 * @brief Hands a whole canvas to the host. An empty table returns false, never
 *        crashes. Templates on the canvas type, same lesson as Pen.
 *        Requirement on CanvasT: kWidth, kHeight, kStrideBytes, pixels().
 */
template <typename CanvasT>
[[nodiscard]] bool submit_frame(const HostTable& host, const CanvasT& canvas) noexcept {
    if (host.flush == nullptr) {
        return false;
    }
    const FlushRegion region{
        .rect = base::RectI{.pos = base::PointI{0, 0},
                            .size = base::SizeI{.w = static_cast<int>(CanvasT::kWidth),
                                                .h = static_cast<int>(CanvasT::kHeight)}},
        .pixels = canvas.pixels().data(),
        .stride_bytes = CanvasT::kStrideBytes};
    host.flush(host.context, region);
    return true;
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

/**
 * @brief A fake screen living in PC memory: the first host to fill the table.
 *
 * Receives full frames, counts what it refuses, and can dump itself as a PBM
 * image. The OLED and Cinux hosts of later tiers grow from this same shape:
 * fill the table, keep a frame, decode nothing but pixels.
 */
class OffscreenHost {
  public:
    OffscreenHost(int width, int height);

    /// The table a canvas submits to; handing this to the core is all a host is.
    /// Building the channel is not a const act: the table carries mutable access.
    [[nodiscard]] HostTable table() noexcept;

    /// Reads a received pixel; out-of-bounds reads as Off.
    [[nodiscard]] canvas::Color get(base::PointI p) const noexcept;

    /// Successful full-frame receptions.
    [[nodiscard]] int flush_count() const noexcept;

    /// Refused regions; ch1 accepts full frames of the right shape only.
    [[nodiscard]] int rejected_count() const noexcept;

    /// Writes the received frame as a binary PBM (P4), the debug image format.
    [[nodiscard]] bool save_pbm(std::string_view path) const noexcept;

  private:
    static void flush_thunk(void* context, const FlushRegion& region) noexcept;
    void handle_flush(const FlushRegion& region) noexcept;

    int width_ = 0;
    int height_ = 0;
    int flush_count_ = 0;
    int rejected_count_ = 0;
    std::vector<std::uint8_t> frame_; // same packing as the canvas, MSB first
};

/// @brief Writes rows as a binary PBM (P4): "P4\n<w> <h>\n", then packed rows.
[[nodiscard]] bool write_pbm(std::string_view path, int width, int height,
                             std::span<const std::uint8_t> rows, std::size_t stride_bytes) noexcept;

} // namespace visor::host
