#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <print>
#include <span>
#include <string>
#include <utility>

#include "cases.hpp"
#include "testing.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/posix/spawn.hpp"
#include "visor/bindings/spawn.hpp"

namespace {

/// One child's whole answer: the words it sent down the gifted pipe plus the
/// exit word it left behind, in the pair every birth judgment wants.
struct Answer {
    std::string words;
    int exit_status = -1;
};

/// Drains the pipe until the child's end closes, then reaps the child. The
/// read blocks until EOF, so the answer arrives whole or the judgment hangs
/// -- either way the truth gets said.
Answer collect(int read_end, pid_t child) noexcept {
    std::string words;
    for (;;) {
        std::array<char, 64> chunk{};
        const ssize_t got = ::read(read_end, chunk.data(), chunk.size());
        if (got <= 0) {
            break;
        }
        words.append(chunk.data(), static_cast<std::size_t>(got));
    }
    ::close(read_end);
    int status = 0;
    ::waitpid(child, &status, 0);
    return {.words = std::move(words), .exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1};
}

} // namespace

void run_spawn_tests() {
    using visor::host::FdGift;
    using visor::host::HostTable;
    using visor::host::spawn_via_posix;
    using visor::host::SpawnRequest;

    // The unwired stance: an empty table cannot birth and says so instead of
    // pretending. The fake and offscreen hosts live here by design -- a
    // spawn-less desk is a judgment desk, not a broken one.
    const HostTable bare;
    VISOR_CHECK(!bare.can_spawn());
    VISOR_CHECK_EQ(bare.spawn_child(SpawnRequest{}), -1);

    // The live judgments raise /bin/sh; an environment without it still
    // deserves the unwired stance above, said loudly.
    if (::access("/bin/sh", X_OK) != 0) {
        std::println("SKIP host/spawn: no /bin/sh to raise");
        return;
    }

    HostTable table;
    table.spawn = &spawn_via_posix;
    VISOR_CHECK(table.can_spawn());

    // The redirect shape (the private-channel trick): a pipe's write end
    // lands at the child's stdout, and the words come back through it.
    {
        std::array<int, 2> channel{};
        VISOR_CHECK(::pipe(channel.data()) == 0);
        const FdGift gift{.child_fd = 1, .parent_fd = channel[1]};
        const std::array argv{"/bin/sh", "-c", "printf ok"};
        const SpawnRequest request{
            .path = "/bin/sh",
            .argv = argv,
            .env = {},
            .gifts = std::span<const FdGift>(&gift, 1),
            .close_other_fds = false,
        };
        const int child = table.spawn_child(request);
        VISOR_CHECK(child > 0);
        ::close(channel[1]); // the caller's half is the caller's to close
        const Answer answer = collect(channel[0], static_cast<pid_t>(child));
        VISOR_CHECK_EQ(answer.exit_status, 0);
        VISOR_CHECK_EQ(answer.words, std::string("ok"));
    }

    // The CLOEXEC survival: a gift promised to the child must live through
    // the exec even when the parent marked it CLOEXEC for its own hygiene
    // (the bridge's sockets all carry the mark). The pass-as-is gift gets
    // the seal taken off in the child, and the words still come back
    // through the marked descriptor's own number.
    {
        std::array<int, 2> channel{};
        VISOR_CHECK(::pipe2(channel.data(), O_CLOEXEC) == 0);
        std::array<char, 64> script{};
        static_cast<void>(
            std::snprintf(script.data(), script.size(), "printf %%s marked >&%d", channel[1]));
        const FdGift gift{.child_fd = channel[1], .parent_fd = channel[1]};
        const std::array<const char*, 3> argv{"/bin/sh", "-c", script.data()};
        const SpawnRequest request{
            .path = "/bin/sh",
            .argv = argv,
            .env = {},
            .gifts = std::span<const FdGift>(&gift, 1),
            .close_other_fds = false,
        };
        const int child = table.spawn_child(request);
        VISOR_CHECK(child > 0);
        ::close(channel[1]);
        const Answer answer = collect(channel[0], static_cast<pid_t>(child));
        VISOR_CHECK_EQ(answer.exit_status, 0);
        VISOR_CHECK_EQ(answer.words, std::string("marked"));
    }

    // The pass-as-is shape with a replaced environment: the gift keeps its
    // own number across the birth, the whitelist stance preserves exactly
    // the gifted numbers, and only the request's env words reach the child
    // (the parent exports no VISOR_SPAWN_MARK, so "carry" can only arrive
    // through the table).
    {
        std::array<int, 2> channel{};
        VISOR_CHECK(::pipe(channel.data()) == 0);
        std::array<char, 96> script{};
        static_cast<void>(std::snprintf(script.data(), script.size(),
                                        "printf %%s $VISOR_SPAWN_MARK >&%d", channel[1]));
        const FdGift gift{.child_fd = channel[1], .parent_fd = channel[1]};
        const std::array<const char*, 3> argv{"/bin/sh", "-c", script.data()};
        const std::array env_words{"VISOR_SPAWN_MARK=carry"};
        const SpawnRequest request{
            .path = "/bin/sh",
            .argv = argv,
            .env = env_words,
            .gifts = std::span<const FdGift>(&gift, 1),
            .close_other_fds = true,
        };
        const int child = table.spawn_child(request);
        VISOR_CHECK(child > 0);
        ::close(channel[1]);
        const Answer answer = collect(channel[0], static_cast<pid_t>(child));
        VISOR_CHECK_EQ(answer.exit_status, 0);
        VISOR_CHECK_EQ(answer.words, std::string("carry"));
    }

    // The fat-env shape: a desktop's inherited environment runs to dozens
    // of words on its own, and the bridge's stance hands the whole list
    // plus its overrides, so the budget must hold a real environment. One
    // hundred filler words ride along and the marked word still arrives --
    // a 64-word budget would have refused the birth outright.
    {
        std::array<int, 2> channel{};
        VISOR_CHECK(::pipe(channel.data()) == 0);
        std::array<std::array<char, 24>, 100> fillers{};
        std::array<const char*, 101> env_words{};
        for (std::size_t i = 0; i < fillers.size(); ++i) {
            static_cast<void>(
                std::snprintf(fillers[i].data(), fillers[i].size(), "SPAWN_FILL_%zu=0", i));
            env_words[i] = fillers[i].data();
        }
        env_words[100] = "VISOR_SPAWN_MARK=fat";
        const FdGift gift{.child_fd = 1, .parent_fd = channel[1]};
        const std::array argv{"/bin/sh", "-c", "printf %s $VISOR_SPAWN_MARK"};
        const SpawnRequest request{
            .path = "/bin/sh",
            .argv = argv,
            .env = env_words,
            .gifts = std::span<const FdGift>(&gift, 1),
            .close_other_fds = false,
        };
        const int child = table.spawn_child(request);
        VISOR_CHECK(child > 0);
        ::close(channel[1]);
        const Answer answer = collect(channel[0], static_cast<pid_t>(child));
        VISOR_CHECK_EQ(answer.exit_status, 0);
        VISOR_CHECK_EQ(answer.words, std::string("fat"));
    }

    // The whitelist stance, both directions on one fixed number: the parent
    // parks a descriptor at 9, and the child reports whether it survived.
    // /proc/self/fd is the Linux eye the probe looks through, which is the
    // environment this judgment was written for.
    const auto probe = [&](bool whitelist) {
        std::array<int, 2> channel{};
        static_cast<void>(::pipe(channel.data()));
        VISOR_CHECK_EQ(::dup2(channel[1], 9), 9);
        const FdGift gift{.child_fd = 1, .parent_fd = channel[1]};
        const char* const probe_script =
            "if test -e /proc/self/fd/9; then printf leak; else printf clean; fi";
        const std::array argv{
            "/bin/sh",
            "-c",
            probe_script,
        };
        const SpawnRequest request{
            .path = "/bin/sh",
            .argv = argv,
            .env = {},
            .gifts = std::span<const FdGift>(&gift, 1),
            .close_other_fds = whitelist,
        };
        const int child = table.spawn_child(request);
        VISOR_CHECK(child > 0);
        ::close(channel[1]);
        ::close(9);
        return collect(channel[0], static_cast<pid_t>(child));
    };
    VISOR_CHECK_EQ(probe(true).words, std::string("clean"));
    VISOR_CHECK_EQ(probe(false).words, std::string("leak"));
}
