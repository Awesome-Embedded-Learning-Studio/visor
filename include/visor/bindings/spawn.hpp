#pragma once

#include <span>

namespace visor::host {

/**
 * @brief One descriptor handover: the child receives a twin of parent_fd at
 *        child_fd.
 * @note  Equal numbers are the pass-as-is shape (the descriptor keeps its
 *        number across the birth, the way a bridge hands its sockets to the
 *        program it raises); different numbers are the redirect shape (the
 *        private-channel trick that turns a socketpair end into stdio).
 */
struct FdGift {
    int child_fd = -1;
    int parent_fd = -1;
};

/**
 * @brief Everything the fourth slot needs to birth one program. The table
 *        only carries the words; the host speaks them.
 * @note  The slot only births, it never reaps: waiting for the child is the
 *        caller's business, and so is closing the caller's own half of every
 *        gift. An empty env inherits the caller's environment whole, which
 *        keeps the plain "run a program" case plain.
 */
struct SpawnRequest {
    /// The program to run, exec semantics: an absolute or resolved path,
    /// never a shell command line.
    const char* path = nullptr;

    /// The argument words, argv[0] included. The terminating null is the
    /// host's to append, so a span suffices.
    std::span<const char* const> argv;

    /// The environment words, "KEY=VALUE" each, verbatim: the host does not
    /// interpret them, the caller's knowledge decides what rides along.
    std::span<const char* const> env;

    /// The descriptors riding along, each landing at its child number.
    std::span<const FdGift> gifts;

    /// True closes every descriptor above stdio except the gift targets
    /// before the program starts -- the whitelist stance a host that owns
    /// many open channels owes the programs it raises.
    bool close_other_fds = false;
};

} // namespace visor::host
