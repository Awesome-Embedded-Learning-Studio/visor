#include "visor/bindings/offscreen/offscreen_host.hpp"

#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/host.hpp"

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/pixel_traits.hpp"

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

// The PC half of the bindings, separated from host.cpp in the same split that
// divided the headers: a freestanding build links host.cpp alone and never
// pulls in the heap-backed double or the file writer.
OffscreenHost::OffscreenHost(int width, int height)
    : OffscreenHost(width, height, PixelFormat::Mono1) {}

OffscreenHost::OffscreenHost(int width, int height, PixelFormat format)
    : width_(width), height_(height), format_(format),
      frame_(row_bytes(format, static_cast<std::size_t>(width)) * static_cast<std::size_t>(height),
             0) {}

HostTable OffscreenHost::table() noexcept {
    return HostTable{
        .flush = &OffscreenHost::flush_thunk,
        .poll_event = &OffscreenHost::poll_event_thunk,
        .clock_ms = &OffscreenHost::clock_thunk,
        .context = this,
        .info =
            HostInfo{
                .screen = base::SizeI{.w = width_, .h = height_},
                .format = format_,
            },
    };
}

void OffscreenHost::flush_thunk(void* context, const FlushRegion& region) noexcept {
    static_cast<OffscreenHost*>(context)->handle_flush(region);
}

void OffscreenHost::handle_flush(const FlushRegion& region) noexcept {
    // The ch11 contract: any in-bounds region of the right format lands, not
    // just whole frames. A strip and a dirty rectangle are as welcome as the
    // full canvas; empty regions, out-of-bounds ones and regions in a format
    // this host does not speak are refused and counted, because each means a
    // sender that got its window or its format wrong.
    const bool acceptable = region.format == format_ && region.pixels != nullptr &&
                            !region.rect.is_empty() && region.rect.pos.x >= 0 &&
                            region.rect.pos.y >= 0 &&
                            region.rect.pos.x + region.rect.size.w <= width_ &&
                            region.rect.pos.y + region.rect.size.h <= height_;
    if (!acceptable) {
        ++rejected_count_;
        return;
    }
    if (format_ != PixelFormat::Mono1) {
        // A color region is whole bytes per pixel, so the copy is a row copy.
        // The source stride comes from the sender (a strip may be narrower
        // than the screen); the destination stride is this host's row.
        const std::size_t dst_stride = row_bytes(format_, static_cast<std::size_t>(width_));
        const std::size_t pixel_bytes = format_ == PixelFormat::RGB565 ? 2U : 4U;
        const std::size_t row_width = static_cast<std::size_t>(region.rect.size.w) * pixel_bytes;
        const std::size_t dst_column = static_cast<std::size_t>(region.rect.pos.x) * pixel_bytes;
        for (int row = 0; row < region.rect.size.h; ++row) {
            const std::uint8_t* src_row =
                region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
            std::uint8_t* dst_row =
                frame_.data() +
                ((static_cast<std::size_t>(region.rect.pos.y) + static_cast<std::size_t>(row)) *
                 dst_stride) +
                dst_column;
            std::copy(src_row, src_row + row_width, dst_row);
        }
        ++flush_count_;
        return;
    }
    const std::size_t stride = (static_cast<std::size_t>(width_) + 7) / 8;
    if (region.rect.pos.x % 8 == 0) {
        // The fast path: a byte-aligned destination column means whole source
        // bytes land on whole destination bytes (both rows are canvas-packed,
        // so a region pixel and its destination pixel sit at the same in-byte
        // position). A width ending mid-byte keeps the bit path's promise for
        // the tail byte's live bits -- and the source's zero padding never
        // touches the neighbor pixels sharing that byte.
        const std::size_t dst_column = static_cast<std::size_t>(region.rect.pos.x) / 8;
        const std::size_t whole_bytes = static_cast<std::size_t>(region.rect.size.w) / 8;
        const int tail_bits = region.rect.size.w % 8;
        // The tail's live bits, one mask built from positions only: pixel j of
        // the tail sits at pixel_bit_position(j), same slot source and target.
        base::bit::BitMask<std::uint8_t> live{};
        for (int bit = 0; bit < tail_bits; ++bit) {
            live.raw = static_cast<std::uint8_t>(
                live.raw | base::bit::mask_bit<std::uint8_t>(
                               canvas::pixel_bit_position(static_cast<std::size_t>(bit)))
                               .raw);
        }
        for (int row = 0; row < region.rect.size.h; ++row) {
            const std::uint8_t* src_row =
                region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
            std::uint8_t* dst_row =
                frame_.data() +
                ((static_cast<std::size_t>(region.rect.pos.y) + static_cast<std::size_t>(row)) *
                 stride) +
                dst_column;
            for (std::size_t byte = 0; byte < whole_bytes; ++byte) {
                dst_row[byte] = src_row[byte];
            }
            if (tail_bits != 0) {
                const base::bit::BitMask<std::uint8_t> dst_word{dst_row[whole_bytes]};
                const base::bit::BitMask<std::uint8_t> src_word{src_row[whole_bytes]};
                // Blend, not overwrite: bits outside the region keep the
                // frame's ink, live bits take the region's. The blend rides
                // unsigned: a bare uint8_t word would promote back to a
                // signed int under the bitwise NOT.
                const unsigned dst_bits = dst_word.raw;
                const unsigned src_bits = src_word.raw;
                const unsigned live_bits = live.raw;
                dst_row[whole_bytes] =
                    static_cast<std::uint8_t>((dst_bits & ~live_bits) | (src_bits & live_bits));
            }
        }
    } else {
        // Bit-true copy one pixel at a time, the correctness floor for any
        // bit offset: region pixels may sit anywhere, the frame packing never
        // changes. The same pixel_bit_position the canvas packs with unpacks
        // here, the formula's one home paying off.
        for (int row = 0; row < region.rect.size.h; ++row) {
            const std::uint8_t* src_row =
                region.pixels + (static_cast<std::size_t>(row) * region.stride_bytes);
            const std::size_t dst_base =
                (static_cast<std::size_t>(region.rect.pos.y) + static_cast<std::size_t>(row)) *
                stride;
            for (int col = 0; col < region.rect.size.w; ++col) {
                const auto src_x = static_cast<std::size_t>(col);
                const auto dst_x = static_cast<std::size_t>(region.rect.pos.x) + src_x;
                const base::bit::BitMask<std::uint8_t> src_word{src_row[src_x / 8]};
                const bool on = src_word.has(
                    base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(src_x)));
                const std::size_t dst_index = dst_base + (dst_x / 8);
                base::bit::BitMask<std::uint8_t> dst_word{frame_[dst_index]};
                dst_word.deposit(base::bit::bit_at(canvas::pixel_bit_position(dst_x)), on ? 1 : 0);
                frame_[dst_index] = dst_word.raw;
            }
        }
    }
    ++flush_count_;
}

int OffscreenHost::flush_count() const noexcept {
    return flush_count_;
}

int OffscreenHost::rejected_count() const noexcept {
    return rejected_count_;
}

bool OffscreenHost::push_event(const Event& event) noexcept {
    if (feed_.push(event)) {
        return true;
    }
    ++dropped_event_count_;
    return false;
}

int OffscreenHost::dropped_event_count() const noexcept {
    return dropped_event_count_;
}

void OffscreenHost::advance_ms(std::uint64_t delta) noexcept {
    now_ms_ += delta;
}

bool OffscreenHost::poll_event_thunk(void* context, Event& out) noexcept {
    return static_cast<OffscreenHost*>(context)->feed_.pop(out);
}

std::uint64_t OffscreenHost::clock_thunk(void* context) noexcept {
    return static_cast<OffscreenHost*>(context)->now_ms_;
}

bool OffscreenHost::get(base::PointI p) const noexcept {
    return format_ == PixelFormat::Mono1 && get_pixel(p) != 0;
}

std::uint32_t OffscreenHost::get_pixel(base::PointI p) const noexcept {
    if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) {
        return 0;
    }
    const auto x = static_cast<std::size_t>(p.x);
    const auto y = static_cast<std::size_t>(p.y);
    const std::uint8_t* row =
        frame_.data() + (y * row_bytes(format_, static_cast<std::size_t>(width_)));
    switch (format_) {
        case PixelFormat::Mono1:
            // Mono1's pixel is a bool, so the widening is "lit or not".
            return canvas::Mono1Traits::load(row, x) ? 1U : 0U;
        case PixelFormat::RGB565:
            return canvas::Rgb565Traits::load(row, x);
        case PixelFormat::XRGB8888:
            return canvas::Xrgb8888Traits::load(row, x);
    }
    return 0;
}

PixelFormat OffscreenHost::format() const noexcept {
    return format_;
}

bool OffscreenHost::save_pbm(std::string_view path) const noexcept {
    if (format_ != PixelFormat::Mono1) {
        return false;
    }
    const std::size_t stride = row_bytes(format_, static_cast<std::size_t>(width_));
    return write_pbm(path, width_, height_, frame_, stride);
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
