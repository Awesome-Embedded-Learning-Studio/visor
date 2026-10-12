#include "cases.hpp"
#include "testing.hpp"
#include "visor/base/text/utf8.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using visor::base::text::decode_utf8_next;
using visor::base::text::kReplacementCodePoint;

/// The walk every consumer runs: collect code points until nullopt. Invalid
/// bytes surface as U+FFFD entries, so the counts below also pin how many
/// bytes each malformed sequence burned.
std::vector<std::uint32_t> decode_all(std::string_view utf8) {
    std::vector<std::uint32_t> out;
    std::size_t pos = 0;
    while (const auto codepoint = decode_utf8_next(utf8, pos)) {
        out.push_back(*codepoint);
    }
    return out;
}

void ascii_passes_through() {
    const auto decoded = decode_all("AB");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{2});
    VISOR_CHECK_EQ(decoded[0], std::uint32_t{0x41});
    VISOR_CHECK_EQ(decoded[1], std::uint32_t{0x42});
}

void two_byte_sequence_decodes() {
    // U+00E9 (e-acute): one lead, one continuation.
    const auto decoded = decode_all("\xc3\xa9");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{1});
    VISOR_CHECK_EQ(decoded[0], std::uint32_t{0xe9});
}

void three_byte_sequence_decodes() {
    // U+4E2D (zhong): the encoding the CJK glyph line will feed.
    const auto decoded = decode_all("\xe4\xb8\xad");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{1});
    VISOR_CHECK_EQ(decoded[0], std::uint32_t{0x4e2d});
}

void four_byte_sequence_decodes() {
    // U+1F600: the top of the range, four bytes.
    const auto decoded = decode_all("\xf0\x9f\x98\x80");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{1});
    VISOR_CHECK_EQ(decoded[0], std::uint32_t{0x1f600});
}

void truncated_tail_never_stalls() {
    // The lead byte promises three bytes, only two arrive: the walk returns
    // one replacement for the lead, then one for the stranded continuation,
    // and reaches the end instead of spinning.
    const auto decoded = decode_all("\xe4\xb8");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{2});
    VISOR_CHECK_EQ(decoded[0], kReplacementCodePoint);
    VISOR_CHECK_EQ(decoded[1], kReplacementCodePoint);
}

void stray_continuation_is_replaced() {
    // A continuation byte with no lead is one replacement; the ASCII after
    // it still decodes on its own. (The split literal keeps the greedy hex
    // escape from swallowing the 'A'.)
    const auto decoded = decode_all("\x80"
                                    "A");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{2});
    VISOR_CHECK_EQ(decoded[0], kReplacementCodePoint);
    VISOR_CHECK_EQ(decoded[1], std::uint32_t{0x41});
}

void overlong_form_is_rejected() {
    // C0/C1 leads can only spell overlong encodings, so they are not lead
    // bytes at all: both bytes burn as replacements.
    const auto decoded = decode_all("\xc0\xaf");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{2});
    VISOR_CHECK_EQ(decoded[0], kReplacementCodePoint);
    VISOR_CHECK_EQ(decoded[1], kReplacementCodePoint);
}

void surrogate_is_rejected() {
    // ED A0 80 would decode into the surrogate block; the ED range check
    // refuses the second byte, and the rest fall as strays.
    const auto decoded = decode_all("\xed\xa0\x80");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{3});
    for (const auto codepoint : decoded) {
        VISOR_CHECK_EQ(codepoint, kReplacementCodePoint);
    }
}

void past_unicode_max_is_rejected() {
    // F4 90 starts past U+10FFFF; F4 only accepts second bytes up to 8F.
    const auto decoded = decode_all("\xf4\x90\x80\x80");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{4});
    for (const auto codepoint : decoded) {
        VISOR_CHECK_EQ(codepoint, kReplacementCodePoint);
    }
}

void empty_string_reports_end() {
    std::size_t pos = 0;
    VISOR_CHECK(!decode_utf8_next("", pos).has_value());
    VISOR_CHECK_EQ(pos, std::size_t{0});
}

void mixed_walk_decodes_in_order() {
    const auto decoded = decode_all("A\xe4\xb8\xadZ");
    VISOR_CHECK_EQ(decoded.size(), std::size_t{3});
    VISOR_CHECK_EQ(decoded[0], std::uint32_t{0x41});
    VISOR_CHECK_EQ(decoded[1], std::uint32_t{0x4e2d});
    VISOR_CHECK_EQ(decoded[2], std::uint32_t{0x5a});
}

// The decoder is constexpr, so the byte-to-codepoint contract also holds at
// compile time, where a font header's static_asserts live.
static_assert([] {
    constexpr std::string_view zhong{"\xe4\xb8\xad"};
    std::size_t pos = 0;
    const auto codepoint = decode_utf8_next(zhong, pos);
    return codepoint.has_value() && *codepoint == 0x4e2d && pos == zhong.size();
}());

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_utf8_tests() {
    ascii_passes_through();
    two_byte_sequence_decodes();
    three_byte_sequence_decodes();
    four_byte_sequence_decodes();
    truncated_tail_never_stalls();
    stray_continuation_is_replaced();
    overlong_form_is_rejected();
    surrogate_is_rejected();
    past_unicode_max_is_rejected();
    empty_string_reports_end();
    mixed_walk_decodes_in_order();
}
