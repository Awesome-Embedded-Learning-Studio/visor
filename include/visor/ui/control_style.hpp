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

[[nodiscard]] constexpr canvas::Color control_background(const Theme* theme,
                                                         canvas::Color ink) noexcept {
    if (theme != nullptr) {
        return theme->background;
    }
    return ink == canvas::Color::On ? canvas::Color::Off : canvas::Color::On;
}

inline void paint_outline(const Painter& painter, base::RectI area, canvas::Color fg,
                          canvas::Color bg, CornerRadius corners = {}) noexcept {
    painter.fill_rounded(area, fg, corners);
    painter.fill_rounded(base::inset(area, 1), bg, inset_corners(corners));
}

} // namespace visor::ui
