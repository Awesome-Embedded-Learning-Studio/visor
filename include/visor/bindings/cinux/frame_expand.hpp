#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/canvas/mono_canvas.hpp"

namespace visor::host::cinux {

/// Ink colors in the panel's XRGB8888 shape (0xFF alpha, one byte per
/// component). Mono's two states map to two caller-chosen words: a lit panel
/// wants On as black, a dark panel the other way round -- the expansion
/// carries no opinion, the adapter picks the pair.
inline constexpr std::uint32_t kXrgbBlack = 0xff00'0000;
inline constexpr std::uint32_t kXrgbWhite = 0xffff'ffff;

/**
 * @brief The Cinux framebuffer as the expansion sees it.
 *
 * pixels is the mmap'd frame (or any double standing in for it), pitch_px
 * the frame's row stride in pixels (the kernel reports bytes; the adapter
 * divides once at setup). The struct carries no ownership: the frame belongs
 * to the host that mapped it.
 */
struct FrameTarget {
    std::uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    std::size_t pitch_px = 0;
};

/**
 * @brief Expands one mono region into the XRGB8888 frame, pure logic.
 *
 * This is the whole display half of the Cinux binding, kept free of
 * mmap/ioctl so a PC judgment can drive it: the adapter reads a FlushRegion
 * off the host table and calls this. rect is in frame coordinates; mono
 * points at the region's own canvas-packed rows (row-major, MSB first,
 * (width+7)/8 bytes each), mono_stride_bytes apart. Parts of rect outside
 * the frame clip silently, the pen's stance. Every mono bit reads through
 * canvas::pixel_bit_position, the one home of that formula -- including on
 * this side of the binding.
 */
void expand_region(const FrameTarget& target, base::RectI rect, const std::uint8_t* mono,
                   std::size_t mono_stride_bytes, std::uint32_t on, std::uint32_t off) noexcept;

} // namespace visor::host::cinux
