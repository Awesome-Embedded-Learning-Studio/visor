#include "cases.hpp"
#include "testing.hpp"

// The wait macros have no entry in the include checker's mapping, so the
// header and its two macros carry a scoped pass here.
#include <sys/wait.h> // NOLINT(misc-include-cleaner)

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

/// std::system's return value is a wait status, not an exit code. The
/// tutorial's hosts are Linux/WSL, so the POSIX decoding is the contract,
/// the fontgen CLI judgments' own helper. The command processor itself is
/// the honest tool here: the judgment drives a compiler the way the X11
/// judgments drive Xvfb.
int run_command(const std::string& command) {
    // NOLINTNEXTLINE(bugprone-command-processor,cert-env33-c)
    const int status = std::system(command.c_str());
    if (status == -1 || !WIFEXITED(status)) { // NOLINT(misc-include-cleaner)
        return -1;
    }
    return WEXITSTATUS(status); // NOLINT(misc-include-cleaner)
}

} // namespace

void run_cross_tests() {
    // The cross toolchains this judgment knows how to drive, in preference
    // order: the Arm GNU toolchain's armv7 compiler first -- the i.MX6ULL
    // station's own target, a 32-bit Linux ARM with unsigned char -- then
    // the same triple on the PATH, then the aarch64 system compiler. A bare
    // name is looked up on the PATH, a path is checked where it stands.
    // Absent toolchains fall through to the next candidate; none at all is
    // a loud failure, the Xvfb stance: a judgment that cannot run reports
    // that, not a pass.
    const std::array<const char*, 3> candidates = {
        "/opt/arm-gnu-toolchain/bin/arm-none-linux-gnueabihf-g++", // the station's own
        "arm-linux-gnueabihf-g++", // the same triple on the PATH, if installed
        "aarch64-linux-gnu-g++",   // the carried 64-bit ARM fallback
    };
    std::string compiler;
    for (const char* name : candidates) {
        const std::string candidate = name;
        const bool reachable =
            candidate.contains('/')
                ? std::filesystem::exists(candidate)
                : run_command("command -v " + candidate + " >/dev/null 2>&1") == 0;
        if (reachable) {
            compiler = candidate;
            break;
        }
    }
    if (compiler.empty()) {
        VISOR_CHECK(false && "no cross toolchain found (arm-none-linux-gnueabihf at /opt, "
                             "arm-linux-gnueabihf-g++, aarch64-linux-gnu-g++)");
        return;
    }

    const std::filesystem::path here = std::filesystem::path(__FILE__).parent_path();
    const std::filesystem::path source = here / "cross_probe.cpp";
    const std::filesystem::path include = here.parent_path() / "include";
    const std::filesystem::path object =
        std::filesystem::temp_directory_path() / "visor-cross-probe.o";
    // The project's own compile line, the same flags the desktop build uses,
    // handed to the cross compiler: the Cinux-linking layers must compile
    // for the target or the judgment stops here.
    const std::string command = compiler + " -std=c++23 -fno-exceptions -fno-rtti -Wall -Wextra" +
                                " -Werror -I" + include.string() + " -c " + source.string() +
                                " -o " + object.string() + " 2>&1";
    VISOR_CHECK_EQ(run_command(command), 0);
    VISOR_CHECK(std::filesystem::exists(object));
    VISOR_CHECK(std::filesystem::file_size(object) > 0);
    std::filesystem::remove(object);
}
