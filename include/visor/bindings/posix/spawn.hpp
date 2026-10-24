#pragma once

#include "visor/bindings/spawn.hpp"

namespace visor::host {

/**
 * @brief The POSIX filling for the fourth slot: fork, land every gift at its
 *        child number, honor the whitelist stance, then execve.
 * @note  The signature matches HostTable::spawn so a table filler can point
 *        at it directly (the context is unused -- a birth needs no host
 *        state beyond the request itself).
 */
int spawn_via_posix(void* context, const SpawnRequest& request) noexcept;

} // namespace visor::host
