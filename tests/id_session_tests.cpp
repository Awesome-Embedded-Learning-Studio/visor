#include "cases.hpp"
#include "testing.hpp"

#include "visor/base/identity/id_session.hpp"

#include <cstdint>

namespace {

/// A policy that does not count by one: it proves IdSession forwards rather
/// than owning the progression.
struct TensGenerator {
    std::uint32_t current = 10;

    [[nodiscard]] std::uint32_t next() noexcept {
        current += 10;
        return current;
    }
};

/// A policy with a constructor argument, forwarded by IdSession.
struct StepGenerator {
    std::uint32_t current;

    explicit StepGenerator(std::uint32_t first) noexcept : current{first} {}

    [[nodiscard]] std::uint32_t next() noexcept {
        current += 5;
        return current;
    }
};

static_assert(visor::base::IdValue<std::uint32_t>);
static_assert(!visor::base::IdValue<bool>);
static_assert(!visor::base::IdValue<int>);
static_assert(visor::base::AtomicIdValue<std::uint32_t>);
static_assert(
    visor::base::IdGenerator<visor::base::AtomicIdGenerator<std::uint32_t>, std::uint32_t>);
static_assert(visor::base::IdGenerator<TensGenerator, std::uint32_t>);
static_assert(visor::base::IdGenerator<StepGenerator, std::uint32_t>);

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) -- registry entry, called from main.cpp.
void run_id_session_tests() {
    using visor::base::IdSession;

    // The default space hands out one, two, three.
    {
        IdSession<std::uint32_t> ids;
        VISOR_CHECK_EQ(ids.next(), 1U);
        VISOR_CHECK_EQ(ids.next(), 2U);
        VISOR_CHECK_EQ(ids.next(), 3U);
    }

    // Separate sessions are separate spaces: both may start at one.
    {
        IdSession<std::uint32_t> first;
        IdSession<std::uint32_t> second;
        VISOR_CHECK_EQ(first.next(), 1U);
        VISOR_CHECK_EQ(second.next(), 1U);
        VISOR_CHECK_EQ(first.next(), 2U);
        VISOR_CHECK_EQ(second.next(), 2U);
    }

    // A start value is honored, and zero is skipped across the wrap.
    {
        IdSession<std::uint8_t> near_end{std::uint8_t{255}};
        VISOR_CHECK_EQ(near_end.next(), std::uint8_t{255});
        VISOR_CHECK_EQ(near_end.next(), std::uint8_t{1});
    }
    {
        IdSession<std::uint16_t> zero_start{std::uint16_t{0}};
        VISOR_CHECK_EQ(zero_start.next(), std::uint16_t{1});
        VISOR_CHECK_EQ(zero_start.next(), std::uint16_t{2});
    }

    // The width is the caller's choice; the counter math stays in that width.
    {
        IdSession<std::uint64_t> wide;
        VISOR_CHECK_EQ(wide.next(), std::uint64_t{1});
        VISOR_CHECK_EQ(wide.next(), std::uint64_t{2});
    }

    // A custom generator owns the progression; IdSession only forwards.
    {
        IdSession<std::uint32_t, TensGenerator> tens;
        VISOR_CHECK_EQ(tens.next(), 20U);
        VISOR_CHECK_EQ(tens.next(), 30U);
    }

    // Generator constructor arguments are forwarded through IdSession.
    {
        IdSession<std::uint32_t, StepGenerator> stepped{100};
        VISOR_CHECK_EQ(stepped.next(), 105U);
        VISOR_CHECK_EQ(stepped.next(), 110U);
    }
}
