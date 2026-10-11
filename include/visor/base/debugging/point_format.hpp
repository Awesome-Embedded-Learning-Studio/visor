#pragma once

#include <format>

#include "visor/base/concepts/arithmetic.hpp"
#include "visor/base/geometry/point.hpp"

/// Formatting support for visor::base::Point: std::print("{}", p) and std::format("{}", p)
/// render "(x, y)". Format specifiers are not supported and are silently ignored. The usual
/// route is to throw format_error from parse() on a non-empty spec, but this project bans
/// exceptions. The standard explicitly allows specializing std::formatter for user types,
/// the two namespace checks below do not recognize this extension point.
// NOLINTBEGIN(bugprone-std-namespace-modification,cert-dcl58-cpp)
template <visor::base::Arithmetic T>
struct std::formatter<visor::base::Point<T>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(const visor::base::Point<T>& p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};
// NOLINTEND(bugprone-std-namespace-modification,cert-dcl58-cpp)
