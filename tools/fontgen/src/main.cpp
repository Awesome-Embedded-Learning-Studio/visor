#include "emit.hpp"
#include "pbm.hpp"

#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>

namespace {

// Usage problems (wrong argument shape, unusable name) return 2; conversion
// failures (unreadable input, bad PBM) return 1. The split lets scripts tell
// "I typed the command wrong" from "the file is wrong".
constexpr int kUsageExit = 2;
constexpr int kFailureExit = 1;

void print_usage() {
    std::println(stderr, "usage: fontgen <input.pbm> <output.hpp> [--name <identifier>]");
    std::println(stderr, "  Converts a binary PBM image into a C++ header holding the packed 1bpp");
    std::println(stderr,
                 "  array in namespace visor::assets::<name>. Without --name the identifier");
    std::println(stderr, "  is the input file's basename without its extension.");
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

} // namespace

int main(int argc, char** argv) {
    // Exactly "<in> <out>" or "<in> <out> --name <id>"; anything else is a
    // usage error, checked before any file is touched.
    if (argc != 3 && argc != 5) {
        print_usage();
        return kUsageExit;
    }
    const std::string_view input_path = argv[1];
    const std::string_view output_path = argv[2];
    std::string name;
    if (argc == 5) {
        if (std::string_view{argv[3]} != "--name") {
            print_usage();
            return kUsageExit;
        }
        name = argv[4];
    } else {
        name = default_name_from(input_path);
    }

    if (!visor::fontgen::valid_asset_name(name)) {
        std::println(stderr,
                     "fontgen: asset name '{}' is not a usable identifier; pass --name <id>", name);
        return kUsageExit;
    }

    const auto bytes = visor::fontgen::read_binary_file(input_path);
    if (!bytes.has_value()) {
        std::println(stderr, "fontgen: {}", bytes.error().message);
        return kFailureExit;
    }
    const auto image = visor::fontgen::parse_pbm(*bytes);
    if (!image.has_value()) {
        std::println(stderr, "fontgen: {}: {}", input_path, image.error().message);
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
