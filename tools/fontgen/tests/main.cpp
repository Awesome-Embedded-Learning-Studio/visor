#include <print>
#include <string>
#include <string_view>

#include "support.hpp"

namespace visor::fontgen::testing {

void run_bdf_tests();
void run_emit_tests();
void run_font_tests();
void run_pbm_tests();
bool run_cli_tests(const std::string& fontgen_path);

} // namespace visor::fontgen::testing

int main(int argc, char** argv) {
    ::visor::fontgen::testing::run_bdf_tests();
    ::visor::fontgen::testing::run_emit_tests();
    ::visor::fontgen::testing::run_font_tests();
    ::visor::fontgen::testing::run_pbm_tests();
    if (argc == 3 && std::string_view{argv[1]} == "--cli") {
        static_cast<void>(::visor::fontgen::testing::run_cli_tests(argv[2]));
    }
    if (::visor::fontgen::testing::failure_count() != 0) {
        std::println(stderr, "fontgen.tests: {} failure(s)",
                     ::visor::fontgen::testing::failure_count());
        return 1;
    }
    std::println("fontgen.tests: all green");
    return 0;
}
