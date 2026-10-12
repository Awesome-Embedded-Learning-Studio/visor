#include "pbm.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace visor::fontgen {
namespace {

// Side cap for both dimensions. 65536 * 8192 payload bytes still fits a
// 32-bit size_t, so the need computation cannot overflow while parsing.
constexpr std::size_t kMaxSide = std::size_t{1} << 16;

// Refuses to buffer anything bigger than 512 MiB. Real inputs are a few
// hundred bytes; a wrong file (a disk image, say) should fail loudly here
// rather than as a mysterious allocation death later.
constexpr std::size_t kMaxFileBytes = std::size_t{1} << 29;

// PBM whitespace: space, TAB, CR, LF, VT, FF (netpbm header grammar).
bool is_pbm_space(std::uint8_t c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x0b || c == 0x0c;
}

/// Byte cursor with the two parses a PBM header needs: skip whitespace and
/// comments, then take a token. Every step reports a cursor offset in its
/// error wording so a broken file can be located by eye.
class Cursor {
  public:
    explicit Cursor(std::span<const std::uint8_t> bytes) noexcept : bytes_{bytes} {}

    [[nodiscard]] bool at_end() const noexcept { return pos_ >= bytes_.size(); }
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - pos_; }
    [[nodiscard]] std::uint8_t peek() const noexcept { return bytes_[pos_]; }
    void advance() noexcept { ++pos_; }

    void skip_space_and_comments() noexcept {
        while (!at_end()) {
            if (is_pbm_space(peek())) {
                advance();
                continue;
            }
            if (peek() == '#') {
                while (!at_end() && peek() != '\n') {
                    advance();
                }
                continue;
            }
            return;
        }
    }

    [[nodiscard]] std::expected<std::string_view, PbmError> take_token() noexcept {
        skip_space_and_comments();
        if (at_end()) {
            return std::unexpected{PbmError{"expected another header token, file ends early"}};
        }
        const std::size_t start = pos_;
        while (!at_end() && !is_pbm_space(peek()) && peek() != '#') {
            advance();
        }
        // Header tokens are ASCII, so the byte slice crosses into string_view
        // unchanged; from_chars and the comparisons below want characters.
        return std::string_view{reinterpret_cast<const char*>(bytes_.data() + start), pos_ - start};
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

/// Parses one decimal header number. from_chars, not strto*: it takes no
/// locale, throws nothing, and reports overflow as an error code.
[[nodiscard]] std::expected<std::size_t, PbmError> parse_side(std::string_view token,
                                                              std::string_view what) {
    std::size_t value = 0;
    const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{} || end != token.data() + token.size()) {
        return std::unexpected{PbmError{std::string{what} + " must be a decimal number, got '" +
                                        std::string{token} + "'"}};
    }
    if (value == 0 || value > kMaxSide) {
        return std::unexpected{PbmError{std::string{what} + " must be between 1 and " +
                                        std::to_string(kMaxSide) + ", got " +
                                        std::to_string(value)}};
    }
    return value;
}

/// Masks the padding bits of every row to zero. The valid pixels of a
/// not-byte-aligned row sit in the top (width % 8) bits of the last byte, so
/// the tail below them is cleared. Bit arithmetic is spelled out here instead
/// of riding visor::base's masks on purpose: this tool is built and accepted
/// as a project independent of the GUI core, and that boundary is worth more
/// than sharing five lines of shifts.
void clean_padding_bits(MonoImage& image) {
    const std::size_t tail_bits = image.width % 8;
    if (tail_bits == 0 || image.stride() == 0) {
        return;
    }
    const auto keep_mask = static_cast<std::uint8_t>(0xFFU << (8 - tail_bits));
    for (std::size_t row = 0; row < image.height; ++row) {
        std::uint8_t& last = image.rows[(row * image.stride()) + image.stride() - 1];
        last = static_cast<std::uint8_t>(last & keep_mask);
    }
}

} // namespace

std::expected<MonoImage, PbmError> parse_pbm(std::span<const std::uint8_t> bytes) {
    Cursor cursor{bytes};

    const auto magic = cursor.take_token();
    if (!magic.has_value()) {
        return std::unexpected{std::move(magic).error()};
    }
    if (*magic != "P4") {
        return std::unexpected{
            PbmError{"magic must be P4 (binary PBM), got '" + std::string{*magic} + "'"}};
    }

    const auto width = cursor.take_token();
    if (!width.has_value()) {
        return std::unexpected{std::move(width).error()};
    }
    const auto width_value = parse_side(*width, "width");
    if (!width_value.has_value()) {
        return std::unexpected{std::move(width_value).error()};
    }

    const auto height = cursor.take_token();
    if (!height.has_value()) {
        return std::unexpected{std::move(height).error()};
    }
    const auto height_value = parse_side(*height, "height");
    if (!height_value.has_value()) {
        return std::unexpected{std::move(height_value).error()};
    }

    if (cursor.at_end() || !is_pbm_space(cursor.peek())) {
        return std::unexpected{
            PbmError{"exactly one whitespace byte must close the header (found none)"}};
    }
    cursor.advance();

    MonoImage image = make_mono_image(*width_value, *height_value);
    const std::size_t need = image.rows.size();
    const std::size_t remaining = cursor.remaining();
    if (remaining != need) {
        return std::unexpected{PbmError{"payload is " + std::to_string(remaining) +
                                        " bytes, header wants " + std::to_string(need)}};
    }
    std::span<const std::uint8_t> payload = bytes.last(need);
    image.rows.assign(payload.begin(), payload.end());
    clean_padding_bits(image);
    return image;
}

std::expected<std::vector<std::uint8_t>, PbmError> read_binary_file(std::string_view path) {
    // fstream constructors take no string_view directly, so the path crosses
    // an explicit std::filesystem::path — same crossing as the export side.
    std::ifstream in{std::filesystem::path{path}, std::ios::binary | std::ios::ate};
    if (!in.is_open()) {
        return std::unexpected{PbmError{"cannot open '" + std::string{path} + "'"}};
    }
    const std::streamoff end = in.tellg();
    if (end < 0) {
        return std::unexpected{PbmError{"cannot size '" + std::string{path} + "'"}};
    }
    if (static_cast<std::uintmax_t>(end) > kMaxFileBytes) {
        return std::unexpected{PbmError{"'" + std::string{path} + "' is larger than 512 MiB"}};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0);
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!in.good()) {
        return std::unexpected{PbmError{"cannot read '" + std::string{path} + "'"}};
    }
    return bytes;
}

} // namespace visor::fontgen
