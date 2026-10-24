#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/spawn.hpp"

#include <cstddef>
#include <cstdint>

namespace visor::host {

/**
 * @brief The one boundary between the core and the world: hosts fill it in, the
 *        core only ever calls through it. Ch2 carries three callbacks: the
 *        display flush, the event poll, and the millisecond clock, plus the
 *        display surface description. Stage 4 joined a fourth: the spawn
 *        slot, birthing programs with the host's own wiring.
 * @note  poll_event returns false when no event waits, never blocking; what a
 *        full queue drops is the host's decision, not the core's.
 *        clock_ms returns milliseconds since a host-chosen epoch. The value
 *        may wrap, so durations are computed by unsigned subtraction
 *        (now - then), never by comparing absolute values.
 *        spawn returns the child pid, or -1 when the host cannot birth --
 *        an unwired slot is the fake stance, a failed birth cleans up after
 *        itself. The slot only births, it never reaps.
 */
struct HostTable {
    void (*flush)(void* context, const FlushRegion& region) noexcept = nullptr;
    bool (*poll_event)(void* context, Event& out) noexcept = nullptr;
    std::uint64_t (*clock_ms)(void* context) noexcept = nullptr;
    int (*spawn)(void* context, const SpawnRequest& request) noexcept = nullptr;
    void* context = nullptr;
    HostInfo info{};

    /// Submits one region; false when the host wired no flush. The format
    /// defaults to Mono1, which is what every caller before the color chapter
    /// speaks; a color canvas names its own through kWireFormat.
    [[nodiscard]] bool submit_region(base::RectI rect, const std::uint8_t* pixels,
                                     std::size_t stride_bytes,
                                     PixelFormat format = PixelFormat::Mono1) const noexcept;

    /// Hands a whole canvas to the host; same null stance as submit_region.
    template <typename CanvasT>
    [[nodiscard]] bool submit_frame(const CanvasT& canvas) const noexcept {
        const base::RectI full{
            .pos = base::PointI{0, 0},
            .size =
                base::SizeI{
                    .w = static_cast<int>(CanvasT::kWidth),
                    .h = static_cast<int>(CanvasT::kHeight),
                },
        };
        return submit_region(full, canvas.pixels().data(), CanvasT::kStrideBytes,
                             kWireFormat<typename CanvasT::Traits>);
    }

    /// Polls one event; false when the host wired no poll or the queue is empty.
    [[nodiscard]] bool poll(Event& out) const noexcept;

    /// Reads the host clock; a missing clock reads as zero.
    [[nodiscard]] std::uint64_t now_ms() const noexcept;

    /// Whether the host wired a clock.
    [[nodiscard]] bool has_clock() const noexcept;

    /// Births one program through the host's wiring; -1 when the host wired
    /// no spawn (the fake stance) or the birth failed.
    [[nodiscard]] int spawn_child(const SpawnRequest& request) const noexcept;

    /// Whether the host can birth programs at all.
    [[nodiscard]] bool can_spawn() const noexcept;
};

} // namespace visor::host
