#include "visor/bindings/host.hpp"

#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"

#include <cstddef>
#include <cstdint>

namespace visor::host {

// The core-side wrappers from the split: every access to the host table goes
// through a null check, so an empty table degrades to "no events, clock at
// zero" instead of a crash. The offscreen double lives in offscreen_host.cpp.
bool submit_region(const HostTable& host, base::RectI rect, const std::uint8_t* pixels,
                   std::size_t stride_bytes) noexcept {
    if (host.flush == nullptr) {
        return false;
    }
    const FlushRegion region{.rect = rect, .pixels = pixels, .stride_bytes = stride_bytes};
    host.flush(host.context, region);
    return true;
}

bool try_poll_event(const HostTable& host, Event& out) noexcept {
    return host.poll_event != nullptr && host.poll_event(host.context, out);
}

std::uint64_t current_time_ms(const HostTable& host) noexcept {
    return host.clock_ms != nullptr ? host.clock_ms(host.context) : 0;
}

} // namespace visor::host
