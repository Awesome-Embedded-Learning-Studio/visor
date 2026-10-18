#pragma once

#include "visor/base/containers/ring_queue.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace visor::host {

/**
 * @brief A fake screen living in PC memory: the first host to fill the table.
 *
 * Receives any in-bounds region (the full frame is just the largest one),
 * counts what it refuses, feeds scripted events and a scripted clock for the
 * core to poll, and can dump itself as a PBM image. The OLED and Cinux hosts
 * of later tiers grow from this same shape: fill the table, keep a frame,
 * decode nothing but pixels.
 *
 * This header is the PC half of the old bindings/host.hpp, split off in B4: a
 * freestanding build (MCU, Cinux kernel) includes host.hpp for the table alone
 * and never sees the heap container this double runs on. The stdlib whitelist
 * covers the core side; the frame buffer here is an explicit, documented
 * exception that never leaks back into core-side headers.
 */
class OffscreenHost {
  public:
    OffscreenHost(int width, int height);

    /// Total ch2 feed capacity. The ring holds one fewer than this (one slot
    /// stays empty to tell full from empty), so the default feed takes fifteen
    /// events; a fuller host raises the number, nothing else changes.
    static constexpr std::size_t kFeedCapacity = 16;

    /// The table a canvas submits to; handing this to the core is all a host is.
    /// Building the channel is not a const act: the table carries mutable access.
    [[nodiscard]] HostTable table() noexcept;

    /// Reads a received pixel; out-of-bounds reads as unlit.
    [[nodiscard]] bool get(base::PointI p) const noexcept;

    /// Successful region receptions, full frames included.
    [[nodiscard]] int flush_count() const noexcept;

    /// Refused submissions: wrong format, null pixels, an empty region, or a
    /// rectangle leaving the frame.
    [[nodiscard]] int rejected_count() const noexcept;

    /// Feeds one synthetic event for the core to poll through the table.
    /// Returns false when the feed is full: the newcomer is dropped and
    /// counted, the residents untouched. Dropping is a host decision, and a
    /// test double reports its own drops.
    [[nodiscard]] bool push_event(const Event& event) noexcept;

    /// Events the feed refused since construction.
    [[nodiscard]] int dropped_event_count() const noexcept;

    /// Advances the scripted clock. std::uint64_t arithmetic wraps past the
    /// top of its range on purpose: that is the ch2 wraparound lesson, and
    /// durations stay correct under unsigned subtraction.
    void advance_ms(std::uint64_t delta) noexcept;

    /// Writes the received frame as a binary PBM (P4), the debug image format.
    [[nodiscard]] bool save_pbm(std::string_view path) const noexcept;

  private:
    static void flush_thunk(void* context, const FlushRegion& region) noexcept;
    static bool poll_event_thunk(void* context, Event& out) noexcept;
    static std::uint64_t clock_thunk(void* context) noexcept;
    void handle_flush(const FlushRegion& region) noexcept;

    int width_ = 0;
    int height_ = 0;
    int flush_count_ = 0;
    int rejected_count_ = 0;
    int dropped_event_count_ = 0;
    std::uint64_t now_ms_ = 0;
    base::container::RingQueue<Event, kFeedCapacity> feed_;
    std::vector<std::uint8_t> frame_; // same packing as the canvas, MSB first
};

/// @brief Writes rows as a binary PBM (P4): "P4\n<w> <h>\n", then packed rows.
[[nodiscard]] bool write_pbm(std::string_view path, int width, int height,
                             std::span<const std::uint8_t> rows, std::size_t stride_bytes) noexcept;

} // namespace visor::host
