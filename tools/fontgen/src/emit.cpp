#include "emit.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace visor::fontgen {
namespace {

// One short of the traditional linker symbol budget; asset names stay well
// under it in practice, and a fixed cap keeps the failure mode explicit.
constexpr std::size_t kMaxAssetName = 63;

// C++23 keywords plus the alternative operator spellings ([lex.key]). Either
// shape of reserved word produces "namespace visor::assets::while", which only
// breaks at the consumer's compile step, far from the tool run. Both are
// refused here so the mistake dies at fontgen's own exit code.
constexpr std::array<std::string_view, 101> kReservedWords = {
    "alignas",       "alignof",     "and",
    "and_eq",        "asm",         "auto",
    "bitand",        "bitor",       "bool",
    "break",         "case",        "catch",
    "char",          "char8_t",     "char16_t",
    "char32_t",      "class",       "compl",
    "concept",       "const",       "consteval",
    "constexpr",     "constinit",   "const_cast",
    "continue",      "co_await",    "co_return",
    "co_yield",      "decltype",    "default",
    "delete",        "do",          "double",
    "dynamic_cast",  "else",        "enum",
    "explicit",      "export",      "extern",
    "false",         "float",       "for",
    "friend",        "goto",        "if",
    "inline",        "int",         "long",
    "mutable",       "namespace",   "new",
    "noexcept",      "not",         "not_eq",
    "nullptr",       "operator",    "or",
    "or_eq",         "private",     "protected",
    "public",        "register",    "reinterpret_cast",
    "requires",      "return",      "short",
    "signed",        "sizeof",      "static",
    "static_assert", "static_cast", "struct",
    "switch",        "template",    "this",
    "thread_local",  "throw",       "true",
    "try",           "typedef",     "typeid",
    "typename",      "union",       "unsigned",
    "using",         "virtual",     "void",
    "volatile",      "wchar_t",     "while",
    "xor",           "xor_eq",
};

// ASCII only, on purpose: the generated namespace must compile on every host
// toolchain the core targets, and beyond ASCII the set of identifier letters
// is a locale question. <cctype> answers per the C locale, hand-rolled ranges
// answer for the exact set accepted here.
bool is_ascii_letter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ascii_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

// nullopt means the name is fine. An optional, not a zero-valued enum
// sentinel: value-initializing EmitError would equal its first enumerator,
// so "kEmptyName == no error" hides exactly the case it must report.
[[nodiscard]] std::optional<EmitError> classify_name(std::string_view name) noexcept {
    if (name.empty()) {
        return EmitError::kEmptyName;
    }
    if (name.size() > kMaxAssetName) {
        return EmitError::kNameTooLong;
    }
    if (!is_ascii_letter(name.front()) && name.front() != '_') {
        return EmitError::kInvalidCharacters;
    }
    for (const char c : name.substr(1)) {
        if (!is_ascii_letter(c) && !is_ascii_digit(c) && c != '_') {
            return EmitError::kInvalidCharacters;
        }
    }
    if (std::ranges::find(kReservedWords, name) != kReservedWords.end()) {
        return EmitError::kKeyword;
    }
    return std::nullopt;
}

/// Bytes shown per output line. One image row of up to eight bytes stays a
/// single readable line with its row comment; wider rows wrap at the same
/// width so the shape stays regular.
constexpr std::size_t kBytesPerLine = 8;

constexpr std::array<char, 16> kHexDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

constexpr std::array<char, 16> kUpperHexDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                               '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

void append_hex_byte(std::string& out, std::uint8_t value) {
    out += "0x";
    out += kHexDigits[static_cast<std::size_t>(value >> 4)];
    out += kHexDigits[static_cast<std::size_t>(value & 0x0f)];
    out += ", ";
}

/// Lowercase hex, no padding: the codepoint literals in kGlyphs entries.
void append_lower_hex(std::string& out, std::uint32_t value) {
    std::array<char, 8> digits{};
    int length = 0;
    std::uint32_t rest = value;
    do {
        digits[length] = kHexDigits[rest & 0x0f];
        ++length;
        rest >>= 4;
    } while (rest != 0);
    while (length > 0) {
        --length;
        out += digits[length];
    }
}

/// Uppercase hex, zero-padded to at least min_digits: the U+ numbers in
/// comments, four digits for the BMP and six past it.
void append_upper_hex(std::string& out, std::uint32_t value, std::size_t min_digits) {
    int needed = 1;
    for (std::uint32_t rest = value >> 4; rest != 0; rest >>= 4) {
        ++needed;
    }
    const std::size_t digits = std::max(static_cast<std::size_t>(needed), min_digits);
    for (std::size_t index = digits; index > 0; --index) {
        out += kUpperHexDigits[(value >> (4 * (index - 1))) & 0x0f];
    }
}

/// The quoted character rides the codepoint comment for printable ASCII --
/// the range a strip font realistically covers -- with the two escapables
/// escaped. Anything wider shows the U+ number alone.
void append_char_hint(std::string& out, std::uint32_t codepoint) {
    if (codepoint < 0x20 || codepoint > 0x7e) {
        return;
    }
    out += " '";
    if (codepoint == '\\' || codepoint == '\'') {
        out += '\\';
    }
    out += static_cast<char>(codepoint);
    out += '\'';
}

/// "// U+0049 'I'": the glyph's name in a comment.
void append_glyph_tag(std::string& out, std::uint32_t codepoint) {
    out += "// U+";
    append_upper_hex(out, codepoint, 4);
    append_char_hint(out, codepoint);
}

void append_hex_bytes(std::string& out, const MonoImage& image) {
    const std::size_t stride = image.stride();
    for (std::size_t row = 0; row < image.height; ++row) {
        for (std::size_t chunk_start = 0; chunk_start < stride; chunk_start += kBytesPerLine) {
            const std::size_t chunk_end = std::min(stride, chunk_start + kBytesPerLine);
            out += "    ";
            for (std::size_t column = chunk_start; column < chunk_end; ++column) {
                append_hex_byte(out, image.rows[(row * stride) + column]);
            }
            if (chunk_start == 0) {
                out += "// row ";
                out += std::to_string(row);
            }
            out += '\n';
        }
    }
}

} // namespace

const char* describe(EmitError error) noexcept {
    switch (error) {
        case EmitError::kEmptyName:
            return "asset name is empty";
        case EmitError::kNameTooLong:
            return "asset name is longer than 63 characters";
        case EmitError::kInvalidCharacters:
            return "asset name must be [A-Za-z_][A-Za-z0-9_]* (ASCII)";
        case EmitError::kKeyword:
            return "asset name collides with a C++ keyword";
        case EmitError::kBadStorage:
            return "image rows do not hold height * stride bytes";
        case EmitError::kNoGlyphs:
            return "a font header needs at least one glyph";
    }
    return "unknown emit error";
}

bool valid_asset_name(std::string_view name) noexcept {
    return !classify_name(name).has_value();
}

std::expected<std::string, EmitError>
emit_bitmap_header(std::string_view name, std::string_view source_hint, const MonoImage& image) {
    if (const auto name_error = classify_name(name)) {
        return std::unexpected{*name_error};
    }
    if (image.rows.size() != image.height * image.stride()) {
        return std::unexpected{EmitError::kBadStorage};
    }

    std::string out;
    out.reserve(256 + (image.rows.size() * 8));

    out += "// Generated by visor.fontgen from ";
    out += source_hint;
    out += ". Do not hand-edit.\n";
    out += "// Regenerate with\n";
    out += "//   fontgen image ";
    out += source_hint;
    out += " <output header> --name ";
    out += name;
    out += "\n";
    out += "#pragma once\n\n";
    out += "#include <cstddef>\n#include <cstdint>\n\n";
    out += "namespace visor::assets::";
    out += name;
    out += " {\n\n";
    out += "// ";
    out += std::to_string(image.width);
    out += "x";
    out += std::to_string(image.height);
    out += " pixels, stride ";
    out += std::to_string(image.stride());
    out += " byte";
    if (image.stride() != 1) {
        out += 's';
    }
    out += ". Row-major, MSB-first, one bit per pixel; padding bits past each\n";
    out += "// row's last pixel stay zero. Same packing as visor::canvas::Canvas: consumers\n";
    out += "// treat kBits as canvas rows directly.\n";
    out += "inline constexpr std::size_t kWidth = ";
    out += std::to_string(image.width);
    out += ";\n";
    out += "inline constexpr std::size_t kHeight = ";
    out += std::to_string(image.height);
    out += ";\n";
    out += "inline constexpr std::size_t kStrideBytes = ";
    out += std::to_string(image.stride());
    out += ";\n\n";
    out += "inline constexpr std::uint8_t kBits[] = {\n";
    append_hex_bytes(out, image);
    out += "};\n\n";
    out += "} // namespace visor::assets::";
    out += name;
    out += "\n";
    return out;
}

std::expected<std::string, EmitError>
emit_font_header(std::string_view name, std::string_view source_hint,
                 const std::vector<Glyph>& glyphs, std::size_t cell_width, std::size_t cell_height,
                 std::uint32_t first_codepoint) {
    if (const auto name_error = classify_name(name)) {
        return std::unexpected{*name_error};
    }
    if (glyphs.empty()) {
        return std::unexpected{EmitError::kNoGlyphs};
    }
    for (const Glyph& glyph : glyphs) {
        if (glyph.image.rows.size() != glyph.image.height * glyph.image.stride()) {
            return std::unexpected{EmitError::kBadStorage};
        }
    }

    std::size_t total_bytes = 0;
    for (const Glyph& glyph : glyphs) {
        total_bytes += glyph.image.rows.size();
    }
    std::string out;
    out.reserve(512 + (total_bytes * 8) + (glyphs.size() * 128));

    out += "// Generated by visor.fontgen from ";
    out += source_hint;
    out += ". Do not hand-edit.\n// Regenerate with\n//   fontgen font ";
    out += source_hint;
    out += " <output header> --cell ";
    out += std::to_string(cell_width);
    out += "x";
    out += std::to_string(cell_height);
    out += " --first 0x";
    append_lower_hex(out, first_codepoint);
    out += " --name ";
    out += name;
    out += "\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n#include"
           " \"visor/draw/mono_font.hpp\"\n\nnamespace visor::assets::";
    out += name;
    out += " {\n\n// ";
    out += std::to_string(glyphs.size());
    out += " glyphs cut from a ";
    out += std::to_string(cell_width);
    out += "x";
    out += std::to_string(cell_height);
    out += " sheet, line height ";
    out += std::to_string(cell_height);
    out += ". Every glyph's advance is the\n// full cell width and both offsets are zero"
           " (a strip cut); the fields wait\n// for proportional sources. Glyph rows share"
           " kBits, each at its byte_offset,\n// packed like visor::canvas::Canvas rows.\n"
           "inline constexpr std::size_t kLineHeight = ";
    out += std::to_string(cell_height);
    out += ";\n\ninline constexpr std::uint8_t kBits[] = {\n";

    // All glyph rows append to one block; each row wraps at the same eight
    // bytes as an image header's, with the glyph's name on its first chunk.
    for (const Glyph& glyph : glyphs) {
        const std::size_t stride = glyph.image.stride();
        for (std::size_t row = 0; row < glyph.image.height; ++row) {
            for (std::size_t chunk_start = 0; chunk_start < stride; chunk_start += kBytesPerLine) {
                const std::size_t chunk_end = std::min(stride, chunk_start + kBytesPerLine);
                out += "    ";
                for (std::size_t column = chunk_start; column < chunk_end; ++column) {
                    append_hex_byte(out, glyph.image.rows[(row * stride) + column]);
                }
                if (chunk_start == 0) {
                    append_glyph_tag(out, glyph.codepoint);
                    out += " row ";
                    out += std::to_string(row);
                }
                out += '\n';
            }
        }
    }

    out += "};\n\ninline constexpr visor::draw::GlyphEntry kGlyphs[] = {\n";
    std::size_t byte_offset = 0;
    for (const Glyph& glyph : glyphs) {
        out += "    {.codepoint = 0x";
        append_lower_hex(out, glyph.codepoint);
        out += ", .width = ";
        out += std::to_string(glyph.image.width);
        out += ", .height = ";
        out += std::to_string(glyph.image.height);
        out += ", .byte_offset = ";
        out += std::to_string(byte_offset);
        out += ", .advance = ";
        out += std::to_string(cell_width);
        out += ", .offset_x = 0, .offset_y = 0}, ";
        append_glyph_tag(out, glyph.codepoint);
        out += '\n';
        byte_offset += glyph.image.rows.size();
    }
    out += "};\n\n} // namespace visor::assets::";
    out += name;
    out += "\n";
    return out;
}

} // namespace visor::fontgen
