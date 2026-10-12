#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/base/text/utf8.hpp"
#include "visor/draw/mono_bitmap.hpp"

namespace visor::draw {

/**
 * @brief One glyph's address and metrics inside a MonoFont's shared bit block.
 *
 * This is the row shape a generated font header emits. advance may exceed
 * width -- a strip-cut font bakes its inter-glyph gap into the cell, a
 * proportional source carries its own advance -- and the signed offsets place
 * the bitmap relative to the pen so a glyph may overhang its cell. A strip
 * cut fills both offsets with zero; the fields wait for the proportional
 * sources (BDF and friends) the font input line will bring.
 */
struct GlyphEntry {
    std::uint32_t codepoint = 0; ///< Which character this glyph renders.
    std::size_t width = 0;       ///< Glyph bitmap width in pixels.
    std::size_t height = 0;      ///< Glyph bitmap height in pixels.
    std::size_t byte_offset = 0; ///< First byte of this glyph's rows in the font's bits.
    std::size_t advance = 0;     ///< How far the pen moves after this glyph.
    std::int16_t offset_x = 0;   ///< Bitmap left edge relative to the pen, in pixels.
    std::int16_t offset_y = 0;   ///< Bitmap top edge relative to its line's top, in pixels.
};

/**
 * @brief A read-only view over one generated font header.
 *
 * Like MonoBitmap, the view owns nothing: a generated header's kLineHeight,
 * kGlyphs and kBits assemble into it with no copy and no repacking, and the
 * storage's lifetime (the header's) outlives every view over it. Glyph rows
 * live in one shared block, each glyph's rows starting at its byte_offset and
 * packed exactly like visor::canvas::Canvas rows.
 *
 * Precondition, guaranteed by the generator: glyphs are sorted by ascending
 * codepoint, so lookup is a binary search. Duplicate codepoints would make
 * the search's answer arbitrary; the tool refuses them by construction.
 */
struct MonoFont {
    std::size_t line_height = 0; ///< Vertical distance between line tops: the newline step.
    std::span<const GlyphEntry> glyphs;
    std::span<const std::uint8_t> bits;

    /// Binary-searches the codepoint; null when the font has no glyph for it.
    [[nodiscard]] constexpr const GlyphEntry* glyph(std::uint32_t codepoint) const noexcept {
        const auto found = std::ranges::lower_bound(glyphs, codepoint, {}, &GlyphEntry::codepoint);
        if (found == glyphs.end() || found->codepoint != codepoint) {
            return nullptr;
        }
        return &*found;
    }

    /// Assembles one glyph's bitmap view over the shared bit block.
    [[nodiscard]] constexpr MonoBitmap bitmap(const GlyphEntry& entry) const noexcept {
        return MonoBitmap{
            .width = entry.width,
            .height = entry.height,
            .rows = bits.subspan(entry.byte_offset, entry.height * ((entry.width + 7) / 8)),
        };
    }
};

/**
 * @brief The layout cursor: the one home of the text spacing rules.
 *
 * measure_text and ui::draw_text both walk their text through this cursor,
 * so the spacing the measuring tape hears and the spacing the screen sees
 * cannot drift apart -- the same stance that keeps pixel_bit_position the
 * only home of the bit formula. Rules it owns: the pen advances by each
 * glyph's advance, '\n' moves to the next line's top (line_height apart,
 * pen back at the line start), characters with no glyph are skipped without
 * ink and without advance, and a scale below one clamps to one, matching
 * Painter::draw_bitmap's clamp.
 */
class TextLayout {
  public:
    /// Where a glyph paints, relative to the text origin the caller chose.
    struct Placement {
        base::PointI pos;
        const GlyphEntry* glyph = nullptr;
    };

    /// The clamp both consumers share; public so callers can scale what they
    /// measure with exactly the factor the cursor will use.
    [[nodiscard]] static constexpr int clamped(int scale) noexcept { return scale < 1 ? 1 : scale; }

    constexpr TextLayout(const MonoFont& font, std::string_view utf8, int scale) noexcept
        : font_{&font}, utf8_{utf8}, scale_{clamped(scale)} {}

    /**
     * @brief   Decodes the next code point and reports where its glyph paints.
     * @note    nullopt means the text is exhausted. Newlines consume no
     *          placement, and code points the font has no glyph for are
     *          skipped; both still advance the internal walk.
     */
    [[nodiscard]] constexpr std::optional<Placement> next() noexcept {
        while (pos_ < utf8_.size()) {
            const auto codepoint = base::text::decode_utf8_next(utf8_, pos_);
            if (!codepoint.has_value()) {
                break;
            }
            if (*codepoint == kNewline) {
                pen_ = 0;
                ++line_;
                continue;
            }
            const GlyphEntry* entry = font_->glyph(*codepoint);
            if (entry == nullptr) {
                continue;
            }
            const int pen_before = pen_;
            pen_ += static_cast<int>(entry->advance);
            return Placement{
                .pos = base::PointI{(pen_before + entry->offset_x) * scale_,
                                    ((line_ * static_cast<int>(font_->line_height)) +
                                     entry->offset_y) *
                                        scale_},
                .glyph = entry,
            };
        }
        return std::nullopt;
    }

    /// The pen's current position on its line, in scaled pixels: after next()
    /// returns a glyph, this is where the pen sits past that glyph's advance.
    [[nodiscard]] constexpr int pen_x() const noexcept { return pen_ * scale_; }

    /// Lines seen so far, counting the one in progress: a text with no
    /// newline at all has one line, each newline adds one more.
    [[nodiscard]] constexpr std::size_t line_count() const noexcept { return line_ + 1; }

  private:
    static constexpr std::uint32_t kNewline = 0x0a;

    const MonoFont* font_;
    std::string_view utf8_;
    int scale_;
    std::size_t pos_ = 0; // decode position in utf8_
    int pen_ = 0;         // unscaled advance units past the current line's start
    int line_ = 0;
};

/**
 * @brief   The size a block of text occupies in its font, at a scale.
 *
 * Width is the pen's stop after the last glyph of the widest line -- the
 * one-pixel gap an advance carries past the bitmap counts. Height is the
 * line grid: line_count * line_height, so the empty string is one empty
 * line tall and every newline adds a full line_height. Characters without a
 * glyph contribute nothing, per the cursor's rules.
 */
[[nodiscard]] constexpr base::SizeI measure_text(const MonoFont& font, std::string_view utf8,
                                                 int scale) noexcept {
    TextLayout layout{font, utf8, scale};
    int width = 0;
    while (const auto placement = layout.next()) {
        width = std::max(width, layout.pen_x());
    }
    const int height =
        static_cast<int>(layout.line_count() * font.line_height) * TextLayout::clamped(scale);
    return base::SizeI{.w = width, .h = height};
}

} // namespace visor::draw
