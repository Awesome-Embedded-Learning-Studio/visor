#pragma once

#include <cstdint>
#include <format>
#include <initializer_list>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "emit.hpp"
#include "image.hpp"
#include "pbm.hpp"

// The main tree keeps its own testing vocabulary; this tool is built and
// accepted on its own, so its tests carry a sibling-light version of the same
// idea: checks count failures, main returns nonzero when any fired.
namespace visor::fontgen::testing {

inline int& failure_count() noexcept {
    // -fno-threadsafe-statics: the test binary is single-threaded.
    static int count = 0;
    return count;
}

inline void report(const char* file, int line, std::string_view what) {
    ++failure_count();
    std::println(stderr, "{}:{}: FAILED {}", file, line, what);
}

// Value renderings for failure output. uint8_t prints as hex — as a plain
// char it would smear control bytes across the terminal.
inline std::string to_text(std::uint8_t value) {
    return std::format("{:#04x}", value);
}
inline std::string to_text(std::size_t value) {
    return std::to_string(value);
}
inline std::string to_text(int value) {
    return std::to_string(value);
}
inline std::string to_text(bool value) {
    return value ? "true" : "false";
}
inline std::string to_text(std::string_view value) {
    return "'" + std::string{value} + "'";
}
inline std::string to_text(const std::string& value) {
    return to_text(std::string_view{value});
}
inline std::string to_text(EmitError value) {
    return describe(value);
}

// Byte rows print as a count plus the hex sequence, so a round-trip mismatch
// names the first differing byte at a glance.
inline std::string to_text(const std::vector<std::uint8_t>& value) {
    std::string out = std::to_string(value.size()) + " bytes:";
    for (const std::uint8_t byte : value) {
        out += ' ';
        out += to_text(byte);
    }
    return out;
}

template <typename Lhs, typename Rhs>
void report_eq(const char* file, int line, std::string_view lhs_text, std::string_view rhs_text,
               const Lhs& lhs, const Rhs& rhs) {
    ++failure_count();
    std::println(stderr, "{}:{}: CHECK_EQ failed: {} == {}", file, line, lhs_text, rhs_text);
    std::println(stderr, "  lhs: {}", to_text(lhs));
    std::println(stderr, "  rhs: {}", to_text(rhs));
}

// Tests build images through this helper so the byte lists stay readable;
// unsigned int avoids a narrowing complaint per literal.
inline MonoImage image_from(std::size_t width, std::size_t height,
                            std::initializer_list<unsigned int> rows) {
    MonoImage image;
    image.width = width;
    image.height = height;
    image.rows.reserve(rows.size());
    for (const unsigned int byte : rows) {
        image.rows.push_back(static_cast<std::uint8_t>(byte));
    }
    return image;
}

} // namespace visor::fontgen::testing

#define FG_CHECK(cond)                                                    \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ::visor::fontgen::testing::report(__FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (false)

#define FG_CHECK_EQ(lhs, rhs)                                                                \
    do {                                                                                     \
        const auto lhs_value_ = (lhs);                                                       \
        const auto rhs_value_ = (rhs);                                                       \
        if (!(lhs_value_ == rhs_value_)) {                                                   \
            ::visor::fontgen::testing::report_eq(__FILE__, __LINE__, #lhs, #rhs, lhs_value_, \
                                                 rhs_value_);                                \
        }                                                                                    \
    } while (false)
