#include "bdf.hpp"
#include "support.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace visor::fontgen;

/// Bytes from a text fixture; BDF is a line-oriented text format.
std::span<const std::uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

/// Four glyphs covering the shapes that matter: an empty-cell space, an
/// ascender 'A', a descender comma (negative BBX yoff), and a CJK glyph with
/// a two-byte UTF-8 code point -- listed out of codepoint order on purpose,
/// the parser owes sorted output.
std::string_view sample_bdf() {
    return "STARTFONT 2.1\n"
           "FONT -test-fixed-medium-r-normal--12-120-75-75-c-60-iso10646-1\n"
           "SIZE 12 75 75\n"
           "FONTBOUNDINGBOX 6 12 0 -2\n"
           "STARTPROPERTIES 2\n"
           "FONT_ASCENT 10\n"
           "FONT_DESCENT 2\n"
           "ENDPROPERTIES\n"
           "CHARS 5\n"
           "STARTCHAR space\n"
           "ENCODING 32\n"
           "SWIDTH 500 0\n"
           "DWIDTH 6 0\n"
           "BBX 0 0 0 0\n"
           "BITMAP\n"
           "ENDCHAR\n"
           "STARTCHAR A\n"
           "ENCODING 65\n"
           "SWIDTH 500 0\n"
           "DWIDTH 6 0\n"
           "BBX 5 7 0 0\n"
           "BITMAP\n"
           "38\n"
           "44\n"
           "44\n"
           "7c\n"
           "44\n"
           "44\n"
           "44\n"
           "ENDCHAR\n"
           "STARTCHAR comma\n"
           "ENCODING 44\n"
           "SWIDTH 500 0\n"
           "DWIDTH 6 0\n"
           "BBX 2 3 1 -2\n"
           "BITMAP\n"
           "c0\n"
           "c0\n"
           "80\n"
           "ENDCHAR\n"
           "STARTCHAR unencoded\n"
           "ENCODING -1\n"
           "SWIDTH 500 0\n"
           "DWIDTH 6 0\n"
           "BBX 1 1 0 0\n"
           "BITMAP\n"
           "80\n"
           "ENDCHAR\n"
           "STARTCHAR C20013\n"
           "ENCODING 20013\n"
           "SWIDTH 1000 0\n"
           "DWIDTH 12 0\n"
           "BBX 8 8 2 0\n"
           "BITMAP\n"
           "18\n"
           "d8\n"
           "d8\n"
           "f8\n"
           "18\n"
           "3c\n"
           "d8\n"
           "18\n"
           "ENDCHAR\n"
           "ENDFONT\n";
}

void parse_folds_metrics_into_sorted_glyphs() {
    const auto font = parse_bdf(bytes_of(sample_bdf()));
    FG_CHECK(font.has_value());
    if (!font.has_value()) {
        return;
    }
    FG_CHECK_EQ(font->line_height, std::size_t{12}); // FONT_ASCENT 10 + FONT_DESCENT 2
    FG_CHECK_EQ(font->ascent, std::size_t{10});
    // Sorted by codepoint, unencoded (-1) skipped: 32, 44, 65, 20013.
    FG_CHECK_EQ(font->glyphs.size(), std::size_t{4});
    FG_CHECK_EQ(font->glyphs[0].codepoint, std::uint32_t{32});
    FG_CHECK_EQ(font->glyphs[1].codepoint, std::uint32_t{44});
    FG_CHECK_EQ(font->glyphs[2].codepoint, std::uint32_t{65});
    FG_CHECK_EQ(font->glyphs[3].codepoint, std::uint32_t{20013});

    // The empty cell keeps its advance and carries no bits.
    FG_CHECK_EQ(font->glyphs[0].advance, std::size_t{6});
    FG_CHECK_EQ(font->glyphs[0].image.rows.size(), std::size_t{0});

    // 'A': BBX 5 7 0 0 sits on the baseline, top 10-7 rows down the line.
    const Glyph& a = font->glyphs[2];
    FG_CHECK_EQ(a.advance, std::size_t{6});
    FG_CHECK_EQ(a.offset_x, std::int16_t{0});
    FG_CHECK_EQ(a.offset_y, std::int16_t{3});
    FG_CHECK_EQ(a.image.rows,
                (std::vector<std::uint8_t>{0x38, 0x44, 0x44, 0x7c, 0x44, 0x44, 0x44}));

    // The comma hangs below the baseline: yoff -2 with height 3 puts its top
    // at 10 - (-2 + 3) = 9, one pixel shy of the line bottom.
    const Glyph& comma = font->glyphs[1];
    FG_CHECK_EQ(comma.offset_x, std::int16_t{1});
    FG_CHECK_EQ(comma.offset_y, std::int16_t{9});
    FG_CHECK_EQ(comma.image.rows, (std::vector<std::uint8_t>{0xc0, 0xc0, 0x80}));

    // The CJK glyph: advance 12, bitmap pushed two pixels right and two down.
    const Glyph& zhong = font->glyphs[3];
    FG_CHECK_EQ(zhong.advance, std::size_t{12});
    FG_CHECK_EQ(zhong.offset_x, std::int16_t{2});
    FG_CHECK_EQ(zhong.offset_y, std::int16_t{2});
    FG_CHECK_EQ(zhong.image.rows.size(), std::size_t{8});
}

void parse_refuses_non_bdf_input() {
    const auto font = parse_bdf(bytes_of("hello\nworld\n"));
    FG_CHECK(!font.has_value());
    if (font.has_value()) {
        return;
    }
    FG_CHECK(font.error().message.find("STARTFONT") != std::string::npos);
}

void parse_requires_ascent_and_descent() {
    // Without FONT_ASCENT/DESCENT the line grid is unknown, and guessing it
    // from FONTBOUNDINGBOX would be silently wrong for descenders.
    std::string no_properties = "STARTFONT 2.1\nFONT x\nSIZE 12 75 75\nCHARS 0\nENDFONT\n";
    const auto font = parse_bdf(bytes_of(no_properties));
    FG_CHECK(!font.has_value());
    if (font.has_value()) {
        return;
    }
    FG_CHECK(font.error().message.find("FONT_ASCENT") != std::string::npos);
}

void parse_names_the_glyph_when_rows_do_not_match_bbx() {
    std::string short_bitmap = std::string{sample_bdf()};
    // Drop one '44' row from 'A' (BBX promises 7 rows, 6 remain).
    const std::size_t at = short_bitmap.find("44\n44\n44\nENDCHAR");
    FG_CHECK(at != std::string::npos);
    short_bitmap.erase(at + 3, 3);
    const auto font = parse_bdf(bytes_of(short_bitmap));
    FG_CHECK(!font.has_value());
    if (font.has_value()) {
        return;
    }
    FG_CHECK(font.error().message.find("A") != std::string::npos);
    FG_CHECK(font.error().message.find("row") != std::string::npos);
}

void parse_names_the_glyph_when_hex_row_is_malformed() {
    std::string bad_hex = std::string{sample_bdf()};
    const std::size_t at = bad_hex.find("38\n");
    FG_CHECK(at != std::string::npos);
    bad_hex.replace(at, 2, "3"); // half a byte
    const auto font = parse_bdf(bytes_of(bad_hex));
    FG_CHECK(!font.has_value());
    if (font.has_value()) {
        return;
    }
    FG_CHECK(font.error().message.find("A") != std::string::npos);
}

void parse_names_the_glyph_missing_its_metrics() {
    std::string no_dwidth = std::string{sample_bdf()};
    const std::size_t at = no_dwidth.find("DWIDTH 6 0\nBBX 5 7 0 0\n");
    FG_CHECK(at != std::string::npos);
    no_dwidth.erase(at, 10); // 'A' reaches BITMAP with no DWIDTH
    const auto font = parse_bdf(bytes_of(no_dwidth));
    FG_CHECK(!font.has_value());
    if (font.has_value()) {
        return;
    }
    FG_CHECK(font.error().message.find("A") != std::string::npos);
    FG_CHECK(font.error().message.find("DWIDTH") != std::string::npos);
}

void parse_refuses_duplicate_encodings() {
    std::string twice = std::string{sample_bdf()};
    const std::size_t at = twice.find("STARTCHAR comma");
    FG_CHECK(at != std::string::npos);
    twice.insert(at, "STARTCHAR A2\nENCODING 65\nDWIDTH 6 0\nBBX 1 1 0 0\nBITMAP\n80\nENDCHAR\n");
    const auto font = parse_bdf(bytes_of(twice));
    FG_CHECK(!font.has_value());
    if (font.has_value()) {
        return;
    }
    FG_CHECK(font.error().message.find("65") != std::string::npos);
}

void parse_survives_trailing_whitespace_and_crlf() {
    // Real CRLF ordering (\r before \n), blank padding lines at both ends.
    std::string crlf;
    for (const char c : sample_bdf()) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    crlf += " \n";
    const auto font = parse_bdf(bytes_of(crlf));
    FG_CHECK(font.has_value());
    if (font.has_value()) {
        FG_CHECK_EQ(font->glyphs.size(), std::size_t{4});
    }
}

/// The subset main() hands to emit: one range over the sample.
std::vector<Glyph> subset(const BdfFont& font, std::uint32_t first, std::uint32_t last) {
    std::vector<Glyph> out;
    for (const Glyph& glyph : font.glyphs) {
        if (glyph.codepoint >= first && glyph.codepoint <= last) {
            out.push_back(glyph);
        }
    }
    return out;
}

void bdf_header_text_is_exact() {
    const auto font = parse_bdf(bytes_of(sample_bdf()));
    FG_CHECK(font.has_value());
    if (!font.has_value()) {
        return;
    }
    const auto selected = subset(*font, 0x2c, 0x41);
    const auto header =
        emit_bdf_font_header("mini", "mini.bdf", 0x2c, 0x41, font->line_height, selected);
    FG_CHECK(header.has_value());
    if (!header.has_value()) {
        return;
    }
    const std::string expected =
        R"(// Generated by visor.fontgen from mini.bdf. Do not hand-edit.
// Regenerate with
//   fontgen bdf mini.bdf <output header> --range 0x2c:0x41 --name mini
#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/draw/mono_font.hpp"

namespace visor::assets::mini {

// 2 glyphs from a BDF font, line height 12. Every glyph carries its own
// advance and offsets from the source font; the bits share kBits, each at
// its byte_offset, packed like visor::canvas::Canvas rows.
inline constexpr std::size_t kLineHeight = 12;

inline constexpr std::uint8_t kBits[] = {
    0xc0, // U+002C ',' row 0
    0xc0, // U+002C ',' row 1
    0x80, // U+002C ',' row 2
    0x38, // U+0041 'A' row 0
    0x44, // U+0041 'A' row 1
    0x44, // U+0041 'A' row 2
    0x7c, // U+0041 'A' row 3
    0x44, // U+0041 'A' row 4
    0x44, // U+0041 'A' row 5
    0x44, // U+0041 'A' row 6
};

inline constexpr visor::draw::GlyphEntry kGlyphs[] = {
    {.codepoint = 0x2c, .width = 2, .height = 3, .byte_offset = 0, .advance = 6, .offset_x = 1, .offset_y = 9}, // U+002C ','
    {.codepoint = 0x41, .width = 5, .height = 7, .byte_offset = 3, .advance = 6, .offset_x = 0, .offset_y = 3}, // U+0041 'A'
};

} // namespace visor::assets::mini
)";
    FG_CHECK_EQ(*header, expected);
}

} // namespace

namespace visor::fontgen::testing {

void run_bdf_tests() {
    parse_folds_metrics_into_sorted_glyphs();
    parse_refuses_non_bdf_input();
    parse_requires_ascent_and_descent();
    parse_names_the_glyph_when_rows_do_not_match_bbx();
    parse_names_the_glyph_when_hex_row_is_malformed();
    parse_names_the_glyph_missing_its_metrics();
    parse_refuses_duplicate_encodings();
    parse_survives_trailing_whitespace_and_crlf();
    bdf_header_text_is_exact();
}

} // namespace visor::fontgen::testing
