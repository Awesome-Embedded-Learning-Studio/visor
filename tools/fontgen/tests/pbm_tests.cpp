#include "pbm.hpp"
#include "support.hpp"

#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace visor::fontgen;

std::vector<std::uint8_t> bytes_of(std::string_view text,
                                   std::initializer_list<unsigned int> payload = {}) {
    std::vector<std::uint8_t> bytes{text.begin(), text.end()};
    for (const unsigned int byte : payload) {
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    return bytes;
}

bool message_contains(const std::expected<MonoImage, PbmError>& result, std::string_view needle) {
    return !result.has_value() && result.error().message.find(needle) != std::string::npos;
}

void parses_the_export_shape() {
    // Exactly what chapter 6's export writes: "P4\n<w> <h>\n" then rows.
    const auto image = parse_pbm(
        bytes_of("P4\n11 5\n", {0xf8, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0xe0, 0x00, 0x00}));
    FG_CHECK(image.has_value());
    if (!image.has_value()) {
        return;
    }
    FG_CHECK_EQ(image->width, std::size_t{11});
    FG_CHECK_EQ(image->height, std::size_t{5});
    FG_CHECK_EQ(image->rows.size(), std::size_t{10});
    // Row 3 occupies rows[6] and rows[7]: first byte 0x01, second byte's top
    // three pixels set and its five padding bits zero.
    FG_CHECK_EQ(image->rows[6], std::uint8_t{0x01});
    FG_CHECK_EQ(image->rows[7], std::uint8_t{0xe0});
}

void dirty_padding_bits_are_cleaned() {
    // The PBM spec says tails "should" be zero, not "must": both rows here
    // carry junk below the last pixel. The canvas packing promises zero
    // tails, so the parser scrubs them on entry instead of propagating them
    // into every generated header.
    const auto image = parse_pbm(bytes_of("P4\n3 2\n", {0xe7, 0x4e}));
    FG_CHECK(image.has_value());
    if (!image.has_value()) {
        return;
    }
    FG_CHECK_EQ(image->rows[0], std::uint8_t{0xe0});
    FG_CHECK_EQ(image->rows[1], std::uint8_t{0x40});
}

void comments_and_loose_whitespace_are_tolerated() {
    // Comments run to end of line and may sit between tokens; token spacing
    // is any whitespace mix. Only the single byte closing the header is
    // strict — the payload starts right after it, so the closer here is one
    // \n even though the tokens before it mixed space and tab.
    const auto image = parse_pbm(bytes_of("P4 # tiny\n 4\t2\n", {0x80, 0x40}));
    FG_CHECK(image.has_value());
    if (image.has_value()) {
        FG_CHECK_EQ(image->width, std::size_t{4});
        FG_CHECK_EQ(image->height, std::size_t{2});
        FG_CHECK_EQ(image->rows[0], std::uint8_t{0x80});
        FG_CHECK_EQ(image->rows[1], std::uint8_t{0x40});
    }
}

void broken_headers_report_their_cause() {
    FG_CHECK(message_contains(parse_pbm(bytes_of("P1\n1 1\n", {0x00})), "P4"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P5\n1 1\n", {0x00})), "P4"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\nw 1\n", {0x00})), "decimal"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n1 x\n", {0x00})), "decimal"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n0 1\n", {0x00})), "between 1 and"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n1 1\n99999\n", {0x00})), "payload"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n")), "ends early"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n3 2")), "whitespace"));
}

void payload_size_mismatches_report_both_counts() {
    // A 4-wide image wants one byte per row; none given, and two given, are
    // both wrong, each with the counts spelled out.
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n4 1\n")), "payload is 0 bytes, header "
                                                                "wants 1"));
    FG_CHECK(message_contains(parse_pbm(bytes_of("P4\n4 1\n", {0xf0, 0x00})),
                              "payload is 2 bytes, header wants 1"));
}

void export_import_round_trip() {
    // Clean input comes back untouched, byte for byte: that is the export's
    // and the importer's shared contract. Scrubbing dirty tails is the
    // previous case's business, not this one's.
    const MonoImage chevron =
        testing::image_from(11, 5, {0xf8, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0xe0, 0x00, 0x00});
    std::vector<std::uint8_t> pbm = bytes_of("P4\n11 5\n");
    pbm.insert(pbm.end(), chevron.rows.begin(), chevron.rows.end());
    const auto back = parse_pbm(pbm);
    FG_CHECK(back.has_value());
    if (back.has_value()) {
        FG_CHECK_EQ(back->width, chevron.width);
        FG_CHECK_EQ(back->height, chevron.height);
        FG_CHECK_EQ(back->rows, chevron.rows);
    }
}

} // namespace

namespace visor::fontgen::testing {

void run_pbm_tests() {
    parses_the_export_shape();
    dirty_padding_bits_are_cleaned();
    comments_and_loose_whitespace_are_tolerated();
    broken_headers_report_their_cause();
    payload_size_mismatches_report_both_counts();
    export_import_round_trip();
}

} // namespace visor::fontgen::testing
