#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace visor::base::text {

/// What a malformed sequence decodes to: visible damage instead of silently
/// reparsed bytes. Text consumers render or skip it like any other code point
/// their font has no glyph for.
inline constexpr std::uint32_t kReplacementCodePoint = 0xfffd;

/**
 * @brief   Decodes the next UTF-8 code point, advancing pos past its bytes.
 * @note    nullopt means the end of the string, and only that: a malformed
 *          sequence is data, reported as the replacement code point.
 *
 * Accepted are the well-formed sequences of the Unicode range: ASCII, the
 * two-to-four-byte forms that reject overlong encodings through their lead
 * byte ranges, surrogates, and anything past U+10FFFF. A rejected sequence
 * consumes exactly one byte -- its lead -- so the walk always makes progress
 * and the damage stays local to the bytes that caused it.
 *
 * The bytes' meaning is all this decider owns: where a code point goes on
 * screen is the font layout's business, and '\n' (which never appears inside
 * a multi-byte sequence) is left to the caller as an ordinary code point.
 */
[[nodiscard]] constexpr std::optional<std::uint32_t> decode_utf8_next(std::string_view utf8,
                                                                      std::size_t& pos) noexcept {
    if (pos >= utf8.size()) {
        return std::nullopt;
    }
    const auto lead = static_cast<std::uint8_t>(utf8[pos]);
    if (lead < 0x80) {
        ++pos;
        return lead;
    }

    // What the lead byte promised: how many continuation bytes follow, and
    // the window the first of them may sit in. The window is what rejects
    // overlong encodings (C2.., E0 A0.., F0 90..), surrogates (ED ..9F) and
    // values past U+10FFFF (F4 ..8F) without a separate range check later.
    struct LeadInfo {
        std::size_t extra;
        std::uint8_t min_first;
        std::uint8_t max_first;
    };
    const auto info = [lead] -> std::optional<LeadInfo> {
        if (lead >= 0xc2 && lead <= 0xdf) {
            return LeadInfo{.extra = 1, .min_first = 0x80, .max_first = 0xbf};
        }
        if (lead == 0xe0) {
            return LeadInfo{.extra = 2, .min_first = 0xa0, .max_first = 0xbf};
        }
        if ((lead >= 0xe1 && lead <= 0xec) || (lead >= 0xee && lead <= 0xef)) {
            return LeadInfo{.extra = 2, .min_first = 0x80, .max_first = 0xbf};
        }
        if (lead == 0xed) {
            return LeadInfo{.extra = 2, .min_first = 0x80, .max_first = 0x9f};
        }
        if (lead == 0xf0) {
            return LeadInfo{.extra = 3, .min_first = 0x90, .max_first = 0xbf};
        }
        if (lead >= 0xf1 && lead <= 0xf3) {
            return LeadInfo{.extra = 3, .min_first = 0x80, .max_first = 0xbf};
        }
        if (lead == 0xf4) {
            return LeadInfo{.extra = 3, .min_first = 0x80, .max_first = 0x8f};
        }
        return std::nullopt;
    }();
    if (!info.has_value()) {
        ++pos;
        return kReplacementCodePoint;
    }

    // A two-byte lead keeps 5 payload bits, longer leads keep 4.
    std::uint32_t codepoint = lead & (info->extra == 1 ? 0x1fU : 0x0fU);
    for (std::size_t i = 1; i <= info->extra; ++i) {
        const std::size_t at = pos + i;
        if (at >= utf8.size()) {
            ++pos; // truncated tail: burn the lead, leave the strays to the next calls
            return kReplacementCodePoint;
        }
        const auto byte = static_cast<std::uint8_t>(utf8[at]);
        const bool in_window = byte >= info->min_first && byte <= info->max_first;
        if ((byte & 0xc0U) != 0x80 || (i == 1 && !in_window)) {
            ++pos;
            return kReplacementCodePoint;
        }
        codepoint = (codepoint << 6U) | (byte & 0x3fU);
    }
    pos += info->extra + 1;
    return codepoint;
}

} // namespace visor::base::text
