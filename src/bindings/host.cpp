#include "visor/bindings/host.hpp"
#include "visor/bindings/display.hpp"

#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"

#include <cstddef>
#include <cstdint>

namespace visor::host {

// Every access to the host table goes through a null check, so an empty table
// degrades to "no events, clock at zero" instead of a crash. The offscreen
// double lives in offscreen_host.cpp.
bool HostTable::submit_region(base::RectI rect, const std::uint8_t* pixels,
                              std::size_t stride_bytes, PixelFormat format) const noexcept {
    if (flush == nullptr) {
        return false;
    }
    const FlushRegion region{
        .rect = rect,
        .pixels = pixels,
        .stride_bytes = stride_bytes,
        .format = format,
    };
    flush(context, region);
    return true;
}

bool HostTable::poll(Event& out) const noexcept {
    return poll_event != nullptr && poll_event(context, out);
}

std::uint64_t HostTable::now_ms() const noexcept {
    return clock_ms != nullptr ? clock_ms(context) : 0;
}

bool HostTable::has_clock() const noexcept {
    return clock_ms != nullptr;
}

} // namespace visor::host
