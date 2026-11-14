#pragma once

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/spawn.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace visor::host {

/**
 * @brief How a host knows itself after the last touch of its connection.
 *
 * ok is the only status a healthy round ever sees. protocol_error means the
 * server answered with an error: the link still carries words, but the
 * window may no longer hold them, so the caller should look at its window
 * before trusting another submission. disconnected means the link itself is
 * gone: submissions are refused from then on, and what to do about that is
 * the caller's policy -- report and leave, or birth a new window.
 *
 * The table boundary never carries this state: only an adapter can see a
 * connection, so the status is exposed beside the host's own table.
 */
enum class HostStatus : std::uint8_t {
    Ok = 0,        ///< The last touch answered.
    ProtocolError, ///< The server answered with an error; the link lives on.
    Disconnected,  ///< The link is gone; the host refuses submissions.
};

/**
 * @brief The one boundary between the core and the world: hosts fill it in, the
 *        core only ever calls through it. Ch2 carries three callbacks: the
 *        display flush, the event poll, and the millisecond clock, plus the
 *        display surface description. Stage 4 joined a fourth: the spawn
 *        slot, birthing programs with the host's own wiring.
 *
 *        The table is a plain record and nothing else. The consuming
 *        vocabulary -- the null-checked wrappers below -- lives in the free
 *        functions beside it, so the data a host fills and the policy the
 *        core reads stay two separate things.
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
};

static_assert(std::is_trivially_copyable_v<HostTable>,
              "HostTable is a plain record: slots plus context plus info, "
              "with the behavior living in the free functions beside it");

/// Submits one region; false when the host wired no flush. The format
/// defaults to Mono1, which is what every caller before the color chapter
/// speaks; a color canvas names its own through kWireFormat.
[[nodiscard]] bool submit_region(const HostTable& host, base::RectI rect,
                                 const std::uint8_t* pixels, std::size_t stride_bytes,
                                 PixelFormat format = PixelFormat::Mono1) noexcept;

/// Hands a whole canvas to the host; same null stance as submit_region.
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
    return submit_region(host, full, canvas.pixels().data(), CanvasT::kStrideBytes,
                         kWireFormat<typename CanvasT::Traits>);
}

/// Polls one event; false when the host wired no poll or the queue is empty.
[[nodiscard]] bool poll(const HostTable& host, Event& out) noexcept;

/// Reads the host clock; a missing clock reads as zero.
[[nodiscard]] std::uint64_t now_ms(const HostTable& host) noexcept;

/// Whether the host wired a clock.
[[nodiscard]] bool has_clock(const HostTable& host) noexcept;

/// Births one program through the host's wiring; -1 when the host wired
/// no spawn (the fake stance) or the birth failed.
[[nodiscard]] int spawn_child(const HostTable& host, const SpawnRequest& request) noexcept;

/// Whether the host can birth programs at all.
[[nodiscard]] bool can_spawn(const HostTable& host) noexcept;

} // namespace visor::host
