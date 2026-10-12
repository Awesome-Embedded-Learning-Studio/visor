#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emit.hpp"
#include "font.hpp"

namespace visor::fontgen {

/// A parse failure with ready-to-print wording. A string rather than an
/// enum, same stance as PbmError: every failure names the keyword or glyph
/// that caused it, and no caller branches on the kind.
struct BdfError {
    std::string message;
};

/// One BDF font parsed into the generator's glyph shape.
struct BdfFont {
    std::size_t line_height = 0; ///< FONT_ASCENT + FONT_DESCENT: the line grid.
    std::size_t ascent = 0;      ///< Baseline distance from the line top.
    /// Encoded glyphs only (ENCODING <= 0 is skipped), sorted by ascending
    /// codepoint; duplicates are refused rather than silently reordered.
    std::vector<Glyph> glyphs;
};

/**
 * @brief   Parses a BDF (Bitmap Distribution Format) font buffer.
 *
 * The walk is line-oriented over the whole file: global keywords until
 * STARTPROPERTIES yields FONT_ASCENT/FONT_DESCENT (required -- without them
 * the line grid is unknown and guessing it from FONTBOUNDINGBOX would hang
 * descenders in the wrong rows), then per-glyph STARTCHAR blocks. Inside a
 * block ENCODING names the codepoint (non-positive encodings are skipped
 * without complaint), DWIDTH becomes the advance, and BBX folds into the
 * GlyphEntry frame: offset_x is the BBX x offset, offset_y = ascent -
 * (yoff + h) turns BDF's baseline-up origin into "bitmap top below the
 * line's top". BITMAP rows are hex bytes, MSB-first, exactly like the
 * canvas packing, so they cross unchanged.
 *
 * Unknown keywords and comments are skipped; DWIDTH/BBX missing before
 * BITMAP, row counts disagreeing with BBX, malformed hex, and duplicate
 * encodings are failures that name the glyph.
 */
[[nodiscard]] std::expected<BdfFont, BdfError> parse_bdf(std::span<const std::uint8_t> bytes);

/**
 * @brief   Renders a BDF-sourced glyph subset as a font header.
 *
 * Same output shape as the strip mode -- shared kBits, one
 * visor::draw::GlyphEntry per glyph, the header includes
 * visor/draw/mono_font.hpp -- except every advance and offset is the
 * glyph's own, not the cell's. Glyphs must arrive sorted by codepoint with
 * no duplicates (parse_bdf's subset does); byte offsets are a running sum.
 *
 * @param[in] range_first/range_last  The subset that was selected; they ride
 *            the regenerate comment.
 */
[[nodiscard]] std::expected<std::string, EmitError>
emit_bdf_font_header(std::string_view name, std::string_view source_hint, std::uint32_t range_first,
                     std::uint32_t range_last, std::size_t line_height,
                     const std::vector<Glyph>& glyphs);

} // namespace visor::fontgen
