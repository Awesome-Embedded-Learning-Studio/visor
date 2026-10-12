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

void append_hex_byte(std::string& out, std::uint8_t value) {
    out += "0x";
    out += kHexDigits[static_cast<std::size_t>(value >> 4)];
    out += kHexDigits[static_cast<std::size_t>(value & 0x0f)];
    out += ", ";
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
    out += "//   fontgen ";
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

} // namespace visor::fontgen
