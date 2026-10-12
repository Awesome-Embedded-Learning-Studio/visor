#include "bdf.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "image.hpp"

namespace visor::fontgen {
namespace {

/// One line per step, trimmed of the newline and stray spaces; blank lines
/// come through empty (they are legal padding), nullopt means the buffer
/// ended. BDF is line-oriented text, so this cursor is the whole walk.
class LineCursor {
  public:
    explicit LineCursor(std::span<const std::uint8_t> bytes) : bytes_{bytes} {}

    std::optional<std::string_view> next() {
        if (pos_ >= bytes_.size()) {
            return std::nullopt;
        }
        const std::size_t start = pos_;
        while (pos_ < bytes_.size() && bytes_[pos_] != '\n') {
            ++pos_;
        }
        const std::size_t end = pos_;
        if (pos_ < bytes_.size()) {
            ++pos_; // consume the newline itself
        }
        std::string_view line{reinterpret_cast<const char*>(bytes_.data()) + start, end - start};
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        while (!line.empty() && line.front() == ' ') {
            line.remove_prefix(1);
        }
        return line;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

/// A keyword line is the keyword followed by a space (or the line's end);
/// "STARTCHARSET" is not a STARTCHAR.
bool is_keyword(std::string_view line, std::string_view keyword) {
    return line.starts_with(keyword) &&
           (line.size() == keyword.size() || line[keyword.size()] == ' ');
}

std::optional<long> parse_int(std::string_view token) {
    long value = 0;
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{} || ptr != token.data() + token.size()) {
        return std::nullopt;
    }
    return value;
}

/// The first whitespace-separated token of the text after a keyword.
std::string_view first_token(std::string_view rest) {
    while (!rest.empty() && rest.front() == ' ') {
        rest.remove_prefix(1);
    }
    const std::size_t end = rest.find(' ');
    return rest.substr(0, end == std::string_view::npos ? rest.size() : end);
}

std::optional<std::uint8_t> hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    return std::nullopt;
}

/// A glyph as the file spelled it, before the line grid is known: offset_y
/// needs FONT_ASCENT, which the properties block may precede or follow the
/// glyphs with, so the fold happens once at the end.
struct RawGlyph {
    std::uint32_t codepoint = 0;
    MonoImage image;
    long advance = 0;
    long offset_x = 0;
    long top_offset = 0; // yoff + height, in baseline-up pixels
    std::string name;
};

[[nodiscard]] std::string quoted(const std::string& name) {
    return name.empty() ? "'(unnamed)'" : ("'" + name + "'");
}

} // namespace

std::expected<BdfFont, BdfError> parse_bdf(std::span<const std::uint8_t> bytes) {
    LineCursor lines{bytes};
    auto bail = [](std::string message) { return std::unexpected{BdfError{std::move(message)}}; };

    // Leading blank lines are tolerated; the first keyword must be STARTFONT.
    auto line = lines.next();
    while (line.has_value() && line->empty()) {
        line = lines.next();
    }
    if (!line.has_value() || !is_keyword(*line, "STARTFONT")) {
        return bail("not a BDF font: the first keyword is not STARTFONT");
    }

    long ascent = 0;
    long descent = 0;
    bool have_ascent = false;
    bool have_descent = false;
    std::vector<RawGlyph> raw;

    while ((line = lines.next()).has_value()) {
        if (line->empty() || is_keyword(*line, "COMMENT")) {
            continue;
        }
        if (is_keyword(*line, "STARTPROPERTIES")) {
            // The property count in the value is advisory; ENDPROPERTIES ends
            // the block whatever the count said.
            while ((line = lines.next()).has_value()) {
                if (line->empty() || is_keyword(*line, "COMMENT")) {
                    continue;
                }
                if (is_keyword(*line, "ENDPROPERTIES")) {
                    break;
                }
                if (line->starts_with("FONT_ASCENT")) {
                    const auto value = parse_int(first_token(line->substr(11)));
                    if (!value.has_value()) {
                        return bail("FONT_ASCENT value is not a number");
                    }
                    ascent = *value;
                    have_ascent = true;
                } else if (line->starts_with("FONT_DESCENT")) {
                    const auto value = parse_int(first_token(line->substr(12)));
                    if (!value.has_value()) {
                        return bail("FONT_DESCENT value is not a number");
                    }
                    descent = *value;
                    have_descent = true;
                }
            }
            continue;
        }
        if (is_keyword(*line, "STARTCHAR")) {
            RawGlyph glyph;
            glyph.name = std::string{line->substr(10)};
            long encoding = -1;
            long bbx_w = 0;
            long bbx_h = 0;
            long bbx_x = 0;
            long bbx_y = 0;
            bool have_dwidth = false;
            bool have_bbx = false;
            bool have_bitmap = false;
            while ((line = lines.next()).has_value()) {
                if (line->empty()) {
                    continue;
                }
                if (is_keyword(*line, "ENDCHAR")) {
                    break;
                }
                if (is_keyword(*line, "ENCODING")) {
                    const auto value = parse_int(first_token(line->substr(8)));
                    if (!value.has_value()) {
                        return bail("glyph " + quoted(glyph.name) +
                                    " has an ENCODING that is not a number");
                    }
                    encoding = *value;
                } else if (is_keyword(*line, "DWIDTH")) {
                    const auto value = parse_int(first_token(line->substr(6)));
                    if (!value.has_value()) {
                        return bail("glyph " + quoted(glyph.name) +
                                    " has a DWIDTH that is not a number");
                    }
                    glyph.advance = *value;
                    have_dwidth = true;
                } else if (is_keyword(*line, "BBX")) {
                    // Four numbers: width height xoff yoff. A token cursor
                    // walks the rest of the line; substr arithmetic four deep
                    // would only hide the shape.
                    const std::string_view rest = line->substr(4);
                    std::size_t consumed = 0;
                    std::array<std::optional<long>, 4> parts{};
                    for (auto& part : parts) {
                        while (consumed < rest.size() && rest[consumed] == ' ') {
                            ++consumed;
                        }
                        const std::size_t start = consumed;
                        while (consumed < rest.size() && rest[consumed] != ' ') {
                            ++consumed;
                        }
                        part = parse_int(rest.substr(start, consumed - start));
                    }
                    for (const auto& part : parts) {
                        if (!part.has_value()) {
                            return bail("glyph " + quoted(glyph.name) +
                                        " has a BBX that is not four numbers");
                        }
                    }
                    bbx_w = *parts[0];
                    bbx_h = *parts[1];
                    bbx_x = *parts[2];
                    bbx_y = *parts[3];
                    have_bbx = true;
                } else if (is_keyword(*line, "BITMAP")) {
                    if (!have_dwidth) {
                        return bail("glyph " + quoted(glyph.name) +
                                    " reaches BITMAP with no DWIDTH");
                    }
                    if (!have_bbx) {
                        return bail("glyph " + quoted(glyph.name) + " reaches BITMAP with no BBX");
                    }
                    if (bbx_w < 0 || bbx_h < 0) {
                        return bail("glyph " + quoted(glyph.name) + " has a negative BBX size");
                    }
                    glyph.image = make_mono_image(static_cast<std::size_t>(bbx_w),
                                                  static_cast<std::size_t>(bbx_h));
                    const std::size_t row_bytes = glyph.image.stride();
                    for (std::size_t row = 0; row < glyph.image.height; ++row) {
                        auto hex_line = lines.next();
                        while (hex_line.has_value() && hex_line->empty()) {
                            hex_line = lines.next();
                        }
                        if (!hex_line.has_value()) {
                            return bail("glyph " + quoted(glyph.name) +
                                        " ends inside its BITMAP rows");
                        }
                        if (hex_line->size() != row_bytes * 2) {
                            return bail("glyph " + quoted(glyph.name) + " row " +
                                        std::to_string(row) + " is not " +
                                        std::to_string(row_bytes * 2) + " hex digits");
                        }
                        for (std::size_t byte = 0; byte < row_bytes; ++byte) {
                            const auto high = hex_nibble((*hex_line)[byte * 2]);
                            const auto low = hex_nibble((*hex_line)[(byte * 2) + 1]);
                            if (!high.has_value() || !low.has_value()) {
                                return bail("glyph " + quoted(glyph.name) + " row " +
                                            std::to_string(row) + " is not hex");
                            }
                            glyph.image.rows[(row * row_bytes) + byte] =
                                static_cast<std::uint8_t>(((*high << 4) | *low));
                        }
                    }
                    have_bitmap = true;
                }
                // SWIDTH, VVECTOR and friends carry nothing this tool folds.
            }
            if (!line.has_value()) {
                return bail("glyph " + quoted(glyph.name) + " never closes (no ENDCHAR)");
            }
            if (!have_bitmap) {
                return bail("glyph " + quoted(glyph.name) + " ends without a BITMAP");
            }
            if (encoding <= 0) {
                continue; // unencoded or the default glyph: nothing to name it by
            }
            if (glyph.advance < 0) {
                return bail("glyph " + quoted(glyph.name) +
                            " has a negative DWIDTH, which the font header cannot carry");
            }
            glyph.codepoint = static_cast<std::uint32_t>(encoding);
            glyph.offset_x = bbx_x;
            glyph.top_offset = bbx_y + bbx_h;
            raw.push_back(std::move(glyph));
            continue;
        }
        // STARTFONT's value, FONT, SIZE, FONTBOUNDINGBOX, CHARS, ENDFONT and
        // anything unknown: nothing this tool folds.
    }

    if (!have_ascent || !have_descent) {
        return bail("the font names no FONT_ASCENT/FONT_DESCENT; the line grid is unknown");
    }
    if (ascent < 0 || descent < 0) {
        return bail("FONT_ASCENT/FONT_DESCENT must not be negative");
    }

    std::ranges::sort(
        raw, [](const RawGlyph& a, const RawGlyph& b) { return a.codepoint < b.codepoint; });
    BdfFont font;
    font.ascent = static_cast<std::size_t>(ascent);
    font.line_height = static_cast<std::size_t>(ascent + descent);
    font.glyphs.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i > 0 && raw[i].codepoint == raw[i - 1].codepoint) {
            return bail("two glyphs claim codepoint " + std::to_string(raw[i].codepoint));
        }
        // BDF hangs glyphs off the baseline (y up); the header frame wants the
        // bitmap's top below the line's top (y down). bbx_y + height measures
        // the top above the baseline, ascent - that measures it below the line
        // top, and a descender's negative bbx_y pushes past it as it should.
        const long offset_y = ascent - raw[i].top_offset;
        if (raw[i].offset_x > 32767 || raw[i].offset_x < -32768 || offset_y > 32767 ||
            offset_y < -32768) {
            return bail("glyph " + quoted(raw[i].name) +
                        " has an offset too large for the font header");
        }
        font.glyphs.push_back(Glyph{
            .codepoint = raw[i].codepoint,
            .image = std::move(raw[i].image),
            .advance = static_cast<std::size_t>(raw[i].advance),
            .offset_x = static_cast<std::int16_t>(raw[i].offset_x),
            .offset_y = static_cast<std::int16_t>(offset_y),
        });
    }
    return font;
}

} // namespace visor::fontgen
