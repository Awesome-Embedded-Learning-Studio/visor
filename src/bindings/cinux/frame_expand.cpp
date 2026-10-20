#include "visor/bindings/cinux/frame_expand.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/pixel_traits.hpp"

namespace visor::host::cinux {

void expand_region(const FrameTarget& target, base::RectI rect, const std::uint8_t* mono,
                   std::size_t mono_stride_bytes, std::uint32_t on, std::uint32_t off) noexcept {
    if (target.pixels == nullptr || mono == nullptr || rect.is_empty()) {
        return;
    }
    // Clip the rect to the frame, the pen's silent stance; the region's own
    // bits stay indexed by region coordinates either way.
    const int x0 = std::max(rect.pos.x, 0);
    const int y0 = std::max(rect.pos.y, 0);
    const int x1 = std::min(rect.pos.x + rect.size.w, target.width);
    const int y1 = std::min(rect.pos.y + rect.size.h, target.height);
    for (int y = y0; y < y1; ++y) {
        const std::uint8_t* src_row =
            mono + (static_cast<std::size_t>(y - rect.pos.y) * mono_stride_bytes);
        std::uint32_t* dst_row = target.pixels + (static_cast<std::size_t>(y) * target.pitch_px);
        for (int x = x0; x < x1; ++x) {
            const auto region_x = static_cast<std::size_t>(x - rect.pos.x);
            const base::bit::BitMask<std::uint8_t> word{src_row[region_x / 8]};
            const bool lit =
                word.has(base::bit::mask_bit<std::uint8_t>(canvas::pixel_bit_position(region_x)));
            dst_row[x] = lit ? on : off;
        }
    }
}

} // namespace visor::host::cinux
