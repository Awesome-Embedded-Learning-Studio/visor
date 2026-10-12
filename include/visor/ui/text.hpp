#pragma once

#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/canvas/color.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/painter.hpp"

namespace visor::ui {

/**
 * @brief   Paints one block of text as a run of bitmap blits.
 *
 * Not a Painter verb: text decomposes into draw_bitmap calls with no canvas
 * access of its own, so it stays a free function beside the seam instead of
 * growing the interface every raster profile must reimplement. Layout walks
 * draw::TextLayout -- the same cursor measure_text uses -- so what lands on
 * screen matches what was measured. Parts outside the frame clip silently,
 * scale below one clamps to one (both inherited from the bitmap verb), and
 * characters with no glyph paint nothing, exactly as they measured nothing.
 *
 * @param[in] at    Top-left corner of the text's line grid.
 * @param[in] scale Integer block scale, explicit for the same reason
 *                  draw_bitmap's is: the seam keeps no defaults.
 */
void draw_text(const Painter& painter, base::PointI at, const draw::MonoFont& font,
               std::string_view utf8, canvas::Color ink, int scale) noexcept;

} // namespace visor::ui
