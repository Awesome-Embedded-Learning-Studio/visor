#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "emit.hpp"
#include "font.hpp"
#include "pbm.hpp"

namespace {

// Usage problems (wrong argument shape, unusable name, unparseable numbers)
// return 2; conversion failures (unreadable input, bad PBM, bad sheet
// geometry) return 1. The split lets scripts tell "I typed the command
// wrong" from "the file is wrong".
constexpr int kUsageExit = 2;
constexpr int kFailureExit = 1;

// The highest code point the tool is willing to label a cell with.
constexpr std::uint32_t kMaxCodepoint = 0x10ffff;

void print_usage() {
    std::println(stderr, "usage: fontgen image <input.pbm> <output.hpp> [--name <identifier>]");
    std::println(stderr, "  Converts a binary PBM image into a C++ header holding the packed 1bpp");
    std::println(stderr,
                 "  array in namespace visor::assets::<name>. Without --name the identifier");
    std::println(stderr, "  is the input file's basename without its extension.");
    std::println(stderr,
                 "       fontgen font <sheet.pbm> <output.hpp> --cell <WxH> --first <codepoint>");
    std::println(stderr, "                [--name <identifier>]");
    std::println(stderr,
                 "  Cuts the sheet into WxH cells (row-major scan) and emits a font header:");
    std::println(stderr, "  a glyph table plus a shared bit block in visor::assets::<name>, ready");
    std::println(stderr, "  to assemble into a visor::draw::MonoFont. --first labels the first");
    std::println(stderr, "  cell; write it as hex (0x20) or decimal (32).");
}

// "assets/logo.pbm" -> "logo". The stem keeps whatever characters the
// filename had; validity is checked by the caller, which reports and suggests
// --name when the filename does not spell an identifier.
std::string default_name_from(std::string_view input_path) {
    return std::filesystem::path{input_path}.stem().string();
}

bool write_text_file(std::string_view path, std::string_view contents) {
    // fstream constructors take no string_view directly, so the path crosses
    // an explicit std::filesystem::path, same as the export side in the GUI.
    std::ofstream out{std::filesystem::path{path}, std::ios::binary | std::ios::trunc};
    if (!out.is_open()) {
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return out.good();
}

/// Whole-text decimal, at least one digit: a size that is not a size refuses
/// instead of half-parsing.
std::optional<std::size_t> parse_size(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// "8x16" -> (8, 16). Anything else -- no x, a third part, a non-number, a
/// zero dimension -- is not a cell spec.
std::optional<std::pair<std::size_t, std::size_t>> parse_cell(std::string_view text) {
    const std::size_t split = text.find('x');
    if (split == std::string_view::npos) {
        return std::nullopt;
    }
    const auto width = parse_size(text.substr(0, split));
    const auto height = parse_size(text.substr(split + 1));
    if (!width.has_value() || !height.has_value() || *width == 0 || *height == 0) {
        return std::nullopt;
    }
    return std::pair{*width, *height};
}

/// Hex with a 0x prefix or plain decimal, within the Unicode range.
std::optional<std::uint32_t> parse_codepoint(std::string_view text) {
    std::uint32_t value = 0;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
        if (text.empty()) {
            return std::nullopt;
        }
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            return std::nullopt;
        }
    } else {
        const auto parsed = parse_size(text);
        if (!parsed.has_value() || *parsed > kMaxCodepoint) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*parsed);
    }
    if (value > kMaxCodepoint) {
        return std::nullopt;
    }
    return value;
}

/// What the option pairs said. Zero cells and a zero codepoint mark "not
/// given", which is how the font mode notices a missing requirement.
struct Options {
    std::string name; // empty means "derive from the input file's stem"
    bool has_name = false;
    std::size_t cell_width = 0;
    std::size_t cell_height = 0;
    bool has_cell = false;
    std::uint32_t first_codepoint = 0;
    bool has_first = false;
};

/// Parses "--flag value" pairs after the two positional arguments. False on
/// an unknown flag, a dangling value or a malformed number; whether the font
/// requirements arrived is the mode's business.
bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 4; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return false; // a flag with no value
        }
        const std::string_view flag = argv[i];
        const std::string_view value = argv[i + 1];
        if (flag == "--name") {
            options.name = value;
            options.has_name = true;
        } else if (flag == "--cell") {
            const auto cell = parse_cell(value);
            if (!cell.has_value()) {
                return false;
            }
            options.cell_width = cell->first;
            options.cell_height = cell->second;
            options.has_cell = true;
        } else if (flag == "--first") {
            const auto codepoint = parse_codepoint(value);
            if (!codepoint.has_value()) {
                return false;
            }
            options.first_codepoint = *codepoint;
            options.has_first = true;
        } else {
            return false;
        }
    }
    return true;
}

/// Reads and parses the input once; the modes only differ after this.
std::optional<visor::fontgen::MonoImage> read_image(std::string_view input_path) {
    const auto bytes = visor::fontgen::read_binary_file(input_path);
    if (!bytes.has_value()) {
        std::println(stderr, "fontgen: {}", bytes.error().message);
        return std::nullopt;
    }
    auto image = visor::fontgen::parse_pbm(*bytes);
    if (!image.has_value()) {
        std::println(stderr, "fontgen: {}: {}", input_path, image.error().message);
        return std::nullopt;
    }
    return std::move(*image);
}

// One conversion per subcommand. argv[0] is the mode word, argv[1] and
// argv[2] the input and output paths, the rest option pairs.
int run_image(int argc, char** argv) {
    if (argc < 4) {
        return kUsageExit;
    }
    Options options;
    if (!parse_options(argc, argv, options) || options.has_cell || options.has_first) {
        return kUsageExit; // image mode owns --name only
    }
    const std::string_view input_path = argv[2];
    const std::string_view output_path = argv[3];
    const std::string name = options.has_name ? options.name : default_name_from(input_path);

    if (!visor::fontgen::valid_asset_name(name)) {
        std::println(stderr,
                     "fontgen: asset name '{}' is not a usable identifier; pass --name <id>", name);
        return kUsageExit;
    }
    const auto image = read_image(input_path);
    if (!image.has_value()) {
        return kFailureExit;
    }
    const auto header = visor::fontgen::emit_bitmap_header(name, input_path, *image);
    if (!header.has_value()) {
        std::println(stderr, "fontgen: {}", visor::fontgen::describe(header.error()));
        return kFailureExit;
    }
    if (!write_text_file(output_path, *header)) {
        std::println(stderr, "fontgen: cannot write '{}'", output_path);
        return kFailureExit;
    }
    std::println("fontgen: {}x{} pixels, {} bytes -> {} (visor::assets::{})", image->width,
                 image->height, image->rows.size(), output_path, name);
    return 0;
}

int run_font(int argc, char** argv) {
    if (argc < 4) {
        return kUsageExit;
    }
    Options options;
    if (!parse_options(argc, argv, options) || !options.has_cell || !options.has_first) {
        return kUsageExit; // --cell and --first are the font mode's shape
    }
    const std::string_view input_path = argv[2];
    const std::string_view output_path = argv[3];
    const std::string name = options.has_name ? options.name : default_name_from(input_path);

    if (!visor::fontgen::valid_asset_name(name)) {
        std::println(stderr,
                     "fontgen: asset name '{}' is not a usable identifier; pass --name <id>", name);
        return kUsageExit;
    }
    const auto sheet = read_image(input_path);
    if (!sheet.has_value()) {
        return kFailureExit;
    }
    auto glyphs = visor::fontgen::cut_glyph_sheet(*sheet, options.cell_width, options.cell_height,
                                                  options.first_codepoint);
    if (!glyphs.has_value()) {
        std::println(stderr, "fontgen: {}: {}", input_path,
                     visor::fontgen::describe(glyphs.error()));
        return kFailureExit;
    }
    // The consecutive labels must stay inside the Unicode range: a sheet big
    // enough to run past it was a request the numbers could never serve.
    const std::uint64_t last =
        static_cast<std::uint64_t>(options.first_codepoint) + glyphs->size() - 1;
    if (last > kMaxCodepoint) {
        std::println(stderr, "fontgen: {} glyphs from U+{:x} run past U+10FFFF", glyphs->size(),
                     options.first_codepoint);
        return kUsageExit;
    }
    const auto header =
        visor::fontgen::emit_font_header(name, input_path, *glyphs, options.cell_width,
                                         options.cell_height, options.first_codepoint);
    if (!header.has_value()) {
        std::println(stderr, "fontgen: {}", visor::fontgen::describe(header.error()));
        return kFailureExit;
    }
    if (!write_text_file(output_path, *header)) {
        std::println(stderr, "fontgen: cannot write '{}'", output_path);
        return kFailureExit;
    }
    std::size_t total_bytes = 0;
    for (const auto& glyph : *glyphs) {
        total_bytes += glyph.image.rows.size();
    }
    std::println("fontgen: {} glyphs of {}x{}, {} bytes -> {} (visor::assets::{})", glyphs->size(),
                 options.cell_width, options.cell_height, total_bytes, output_path, name);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // "<mode> <in> <out> [--flags...]" with a pair-shaped flag tail; anything
    // else is a usage error, checked before any file is touched.
    if (argc < 2) {
        print_usage();
        return kUsageExit;
    }
    const std::string_view mode = argv[1];
    if (mode == "image") {
        return run_image(argc, argv);
    }
    if (mode == "font") {
        return run_font(argc, argv);
    }
    print_usage();
    return kUsageExit;
}
