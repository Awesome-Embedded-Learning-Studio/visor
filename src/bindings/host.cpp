#include "visor/bindings/host.hpp"
#include "visor/bindings/events.hpp"

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/mono_canvas.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <print>
#include <span>
#include <string_view>

namespace visor::host {

OffscreenHost::OffscreenHost(int width, int height)
    : width_(width), height_(height),
      frame_(((static_cast<std::size_t>(width) + 7) / 8) * static_cast<std::size_t>(height), 0) {}

HostTable OffscreenHost::table() noexcept {
    return HostTable{.flush = &OffscreenHost::flush_thunk, .context = this};
}

void OffscreenHost::flush_thunk(void* context, const FlushRegion& region) noexcept {
    static_cast<OffscreenHost*>(context)->handle_flush(region);
}

void OffscreenHost::handle_flush(const FlushRegion& region) noexcept {
    const bool full_frame = region.format == PixelFormat::Mono1 && region.rect.pos.x == 0 &&
                            region.rect.pos.y == 0 && region.rect.size.w == width_ &&
                            region.rect.size.h == height_ && region.rect.size.w > 0 &&
                            region.rect.size.h > 0;
    if (!full_frame) {
        ++rejected_count_;
        return;
    }
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    for (int row = 0; row < height_; ++row) {
        const std::size_t src = static_cast<std::size_t>(row) * region.stride_bytes;
        const std::size_t dst = static_cast<std::size_t>(row) * stride;
        std::copy_n(region.pixels + src, stride, frame_.data() + dst);
    }
    ++flush_count_;
}

int OffscreenHost::flush_count() const noexcept {
    return flush_count_;
}

int OffscreenHost::rejected_count() const noexcept {
    return rejected_count_;
}

canvas::Color OffscreenHost::get(base::PointI p) const noexcept {
    if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) {
        return canvas::Color::Off;
    }
    const auto x = static_cast<std::size_t>(p.x);
    const auto y = static_cast<std::size_t>(p.y);
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    const base::bit::BitMask<std::uint8_t> word{frame_[(y * stride) + (x / 8)]};
    const bool on = word.has(base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(x)));
    return on ? canvas::Color::On : canvas::Color::Off;
}

bool OffscreenHost::save_pbm(std::string_view path) const noexcept {
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    return write_pbm(path, width_, height_, frame_, stride);
}

bool try_poll_event(const HostTable& host, Event& out) noexcept {
    return host.poll_event != nullptr && host.poll_event(host.context, out);
}

std::uint64_t current_time_ms(const HostTable& host) noexcept {
    return host.clock_ms != nullptr ? host.clock_ms(host.context) : 0;
}

bool write_pbm(std::string_view path, int width, int height, std::span<const std::uint8_t> rows,
               std::size_t stride_bytes) noexcept {
    // fstream constructors take no string_view directly, so the path crosses an
    // explicit std::filesystem::path.
    std::ofstream out{std::filesystem::path{path}, std::ios::binary | std::ios::trunc};
    if (!out.is_open()) {
        return false;
    }
    // println's trailing newline closes the PBM header line "P4\n<w> <h>\n".
    std::println(out, "P4\n{} {}", width, height);
    const std::size_t row_bytes = (static_cast<std::size_t>(width) + 7) / 8;
    for (int row = 0; row < height; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row) * stride_bytes;
        out.write(reinterpret_cast<const char*>(rows.data() + offset),
                  static_cast<std::streamsize>(row_bytes));
    }
    return out.good();
}

} // namespace visor::host
