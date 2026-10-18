#pragma once

#include <cstdint>

#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/theme.hpp"

namespace visor::ui {

/// The inner edge of a one-pixel rounded outline follows a smaller radius.
[[nodiscard]] constexpr CornerRadius inset_corners(CornerRadius corners) noexcept {
    const auto shrink = [](std::uint8_t radius) {
        return static_cast<std::uint8_t>(radius > 0 ? radius - 1 : 0);
    };
    return {
        .top_left = shrink(corners.top_left),
        .top_right = shrink(corners.top_right),
        .bottom_right = shrink(corners.bottom_right),
        .bottom_left = shrink(corners.bottom_left),
    };
}

/// Fills a rounded outline: the outer shape in edge, the inner hole in ground.
inline void paint_outline(const Painter& painter, base::RectI area, canvas::ColorRole edge,
                          canvas::ColorRole ground, CornerRadius corners = {}) noexcept {
    painter.fill_rounded(area, edge, corners);
    painter.fill_rounded(base::inset(area, 1), ground, inset_corners(corners));
}

} // namespace visor::ui
