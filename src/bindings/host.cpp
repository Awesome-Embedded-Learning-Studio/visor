#include "visor/bindings/host.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/spawn.hpp"

#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"

#include <cstddef>
#include <cstdint>

namespace visor::host {

// Every access to the host table goes through a null check, so an empty table
// degrades to "no events, clock at zero" instead of a crash. These free
// functions are the consuming vocabulary; the table itself carries only the
// slots. The offscreen double lives in offscreen_host.cpp.
bool submit_region(const HostTable& host, base::RectI rect, const std::uint8_t* pixels,
                   std::size_t stride_bytes, PixelFormat format) noexcept {
    if (host.flush == nullptr) {
        return false;
    }
    const FlushRegion region{
        .rect = rect,
        .pixels = pixels,
        .stride_bytes = stride_bytes,
        .format = format,
    };
    host.flush(host.context, region);
    return true;
}

bool poll(const HostTable& host, Event& out) noexcept {
    return host.poll_event != nullptr && host.poll_event(host.context, out);
}

std::uint64_t now_ms(const HostTable& host) noexcept {
    return has_clock(host) ? host.clock_ms(host.context) : 0;
}

bool has_clock(const HostTable& host) noexcept {
    return host.clock_ms != nullptr;
}

int spawn_child(const HostTable& host, const SpawnRequest& request) noexcept {
    return can_spawn(host) ? host.spawn(host.context, request) : -1;
}

bool can_spawn(const HostTable& host) noexcept {
    return host.spawn != nullptr;
}

} // namespace visor::host
