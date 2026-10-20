#pragma once

#include <print>

namespace visor::testing {

/// Cumulative count of failed checks, zero when everything passed. main() maps it to the exit code.
inline int& failure_count() {
    static int count = 0;
    return count;
}

/// Prints one failure line and bumps the counter. Called by the assert macros, not by test bodies.
inline void report(const char* file, int line, const char* detail) {
    ++failure_count();
    std::println(stderr, "FAIL {}:{} {}", file, line, detail);
}

} // namespace visor::testing

/// On a false condition, prints the expression text and counts. No exceptions, no abort.
#define VISOR_CHECK(cond)                                        \
    do {                                                         \
        if (!(cond)) {                                           \
            ::visor::testing::report(__FILE__, __LINE__, #cond); \
        }                                                        \
    } while (false)

/// Asserts both values are equal, prints both expression texts and counts on mismatch.
#define VISOR_CHECK_EQ(actual, expected)                                            \
    do {                                                                            \
        const auto& visor_check_actual_ = (actual);                                 \
        const auto& visor_check_expected_ = (expected);                             \
        if (!(visor_check_actual_ == visor_check_expected_)) {                      \
            ::visor::testing::report(__FILE__, __LINE__, #actual " == " #expected); \
        }                                                                           \
    } while (false)
