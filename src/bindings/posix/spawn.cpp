#include "visor/bindings/posix/spawn.hpp"

#include "visor/bindings/spawn.hpp"

#include <unistd.h>

#include <array>
#include <cstddef>
#include <span>

// execve wants char* arrays while the request speaks const words; the
// kernel only reads them, so the cast is the interface's dress code.
// NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast)

namespace visor::host {
namespace {

// The exec words ride the stack, so a program needing more than this is not
// a desk citizen. The request's spans stay honest: the host appends the
// terminating nulls itself. The env budget is the larger number because an
// inherited desktop environment runs to dozens of words on its own, and the
// inherit-plus-override stance hands the whole list, not a curated one.
constexpr std::size_t kMaxArgWords = 64;
constexpr std::size_t kMaxEnvWords = 256;

/// Closes every descriptor above stdio except the gift targets, the
/// whitelist stance. A closed fd never fails the birth: the program was
/// promised nothing about those numbers.
void close_ungifted(std::span<const FdGift> gifts) noexcept {
    const long ceiling = ::sysconf(_SC_OPEN_MAX);
    for (int fd = 3; fd < static_cast<int>(ceiling); ++fd) {
        bool gifted = false;
        for (const FdGift& gift : gifts) {
            if (gift.child_fd == fd) {
                gifted = true;
                break;
            }
        }
        if (!gifted) {
            ::close(fd);
        }
    }
}

} // namespace

int spawn_via_posix(void* /*context*/, const SpawnRequest& request) noexcept {
    if (request.path == nullptr || request.argv.empty() || request.argv.size() > kMaxArgWords ||
        request.env.size() > kMaxEnvWords) {
        return -1;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        // The child half. A gift with matching numbers needs no dup: the
        // descriptor already survived the fork at its own number. A failed
        // setup never returns to the caller -- exit word 127 is the classic
        // "the exec never happened".
        for (const FdGift& gift : request.gifts) {
            if (gift.child_fd != gift.parent_fd && ::dup2(gift.parent_fd, gift.child_fd) < 0) {
                _exit(127);
            }
        }
        if (request.close_other_fds) {
            close_ungifted(request.gifts);
        }

        std::array<const char*, kMaxArgWords + 1> argv{};
        for (std::size_t i = 0; i < request.argv.size(); ++i) {
            argv[i] = request.argv[i];
        }
        // An empty env inherits the caller's environment whole, which keeps
        // the plain "run a program" case plain.
        std::array<const char*, kMaxEnvWords + 1> envp{};
        for (std::size_t i = 0; i < request.env.size(); ++i) {
            envp[i] = request.env[i];
        }
        ::execve(request.path, const_cast<char* const*>(argv.data()),
                 request.env.empty() ? const_cast<char* const*>(environ)
                                     : const_cast<char* const*>(envp.data()));
        _exit(127);
    }
    return child;
}

} // namespace visor::host
// NOLINTEND(cppcoreguidelines-pro-type-const-cast)
