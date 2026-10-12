#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "image.hpp"

namespace visor::fontgen {

/// One glyph with its layout metrics: which character it renders, its pixels
/// in the canvas packing, and how the pen walks past it. A strip cut fills
/// advance with the cell width and both offsets with zero (the gap is baked
/// into the cells); a BDF glyph brings its own -- advance from DWIDTH,
/// offsets folded from BBX into the GlyphEntry frame.
struct Glyph {
    std::uint32_t codepoint = 0;
    MonoImage image;
    std::size_t advance = 0;   ///< How far the pen moves after this glyph, pixels.
    std::int16_t offset_x = 0; ///< Bitmap left edge relative to the pen, pixels.
    std::int16_t offset_y = 0; ///< Bitmap top edge relative to its line's top, pixels.
};

/// Why a sheet could not be cut into glyphs. Recoverable: the caller reports
/// it, nothing terminates.
enum class CutError {
    kBadCell,       ///< Cell width or height is zero.
    kSheetTooSmall, ///< The sheet holds fewer pixels than one cell.
    kNotDivisible,  ///< Sheet width or height is not a whole multiple of the cell.
};

/// @brief One-line, user-facing wording for each cut failure.
[[nodiscard]] const char* describe(CutError error) noexcept;

/**
 * @brief   Cuts a glyph sheet into per-glyph images.
 *
 * Cells scan row-major (left to right, then the next cell row) and code
 * points run consecutively from first_codepoint, which is how a strip font
 * drawn as one picture maps onto its characters. Each cell row is the
 * matching bit slice of its sheet row, re-packed MSB-first into the cell's
 * own stride, so glyph bits drop into a font header exactly like a
 * whole-image conversion's would.
 */
[[nodiscard]] std::expected<std::vector<Glyph>, CutError>
cut_glyph_sheet(const MonoImage& sheet, std::size_t cell_width, std::size_t cell_height,
                std::uint32_t first_codepoint);

} // namespace visor::fontgen
