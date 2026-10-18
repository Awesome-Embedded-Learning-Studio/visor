#include "visor/ui/text.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/helpers/rect_helper.hpp"

#include <cstddef>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

void draw_text(const Painter& painter, base::PointI at, const draw::MonoFont& font,
               std::string_view utf8, canvas::ColorRole role, int scale) noexcept {
    draw::TextLayout layout{font, utf8, scale};
    while (const auto placement = layout.next()) {
        painter.draw_bitmap(base::PointI{at.x + placement->pos.x, at.y + placement->pos.y},
                            font.bitmap(*placement->glyph), role, draw::TextLayout::clamped(scale));
    }
}

void draw_text_clipped(const Painter& painter, base::RectI clip, base::PointI at,
                       const draw::MonoFont& font, std::string_view utf8, canvas::ColorRole role,
                       int scale) noexcept {
    const int factor = draw::TextLayout::clamped(scale);
    draw::TextLayout layout{font, utf8, factor};
    while (const auto placement = layout.next()) {
        const auto bitmap = font.bitmap(*placement->glyph);
        const base::RectI area{
            .pos = {at.x + placement->pos.x, at.y + placement->pos.y},
            .size =
                {
                    .w = static_cast<int>(bitmap.width) * factor,
                    .h = static_cast<int>(bitmap.height) * factor,
                },
        };
        const auto visible = base::intersect(area, clip);
        if (visible.is_empty()) {
            continue;
        }
        if (visible == area) {
            painter.draw_bitmap(area.pos, bitmap, role, factor);
            continue;
        }
        // Only boundary glyphs need per-pixel clipping; interior glyphs blit directly.
        for (std::size_t y = 0; y < bitmap.height; ++y) {
            for (std::size_t x = 0; x < bitmap.width; ++x) {
                if (bitmap.pixel(x, y)) {
                    const base::RectI pixel{
                        .pos =
                            {
                                area.pos.x + (static_cast<int>(x) * factor),
                                area.pos.y + (static_cast<int>(y) * factor),
                            },
                        .size = {.w = factor, .h = factor},
                    };
                    painter.fill(base::intersect(pixel, clip), role);
                }
            }
        }
    }
}

} // namespace visor::ui
