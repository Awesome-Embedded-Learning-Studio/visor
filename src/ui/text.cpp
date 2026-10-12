#include "visor/ui/text.hpp"

#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

void draw_text(const Painter& painter, base::PointI at, const draw::MonoFont& font,
               std::string_view utf8, canvas::Color ink, int scale) noexcept {
    draw::TextLayout layout{font, utf8, scale};
    while (const auto placement = layout.next()) {
        painter.draw_bitmap(base::PointI{at.x + placement->pos.x, at.y + placement->pos.y},
                            font.bitmap(*placement->glyph), ink, draw::TextLayout::clamped(scale));
    }
}

} // namespace visor::ui
