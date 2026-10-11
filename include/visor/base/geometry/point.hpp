#pragma once

#include <concepts>

#include "visor/base/concepts/arithmetic.hpp"

namespace visor::base {

/**
 * @brief Point bases of the everythings
 *
 * @tparam T must be Arithmetic
 */
template <Arithmetic T>
struct Point {
    T x{};
    T y{};

    Point() = default;
    constexpr Point(T x, T y) : x(x), y(y) {}

    template <Arithmetic U>
        requires(!std::same_as<U, T>)
    constexpr explicit Point(const Point<U>& other)
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

    constexpr bool operator==(const Point&) const = default;

    constexpr Point& operator+=(const Point& rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    constexpr Point& operator-=(const Point& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    constexpr Point& operator*=(T scale) {
        x *= scale;
        y *= scale;
        return *this;
    }

    constexpr Point& operator/=(T scale) {
        x /= scale;
        y /= scale;
        return *this;
    }

    [[nodiscard]] constexpr T dot(const Point& rhs) const { return (x * rhs.x) + (y * rhs.y); }
};

template <Arithmetic T>
constexpr Point<T> operator+(const Point<T>& a, const Point<T>& b) {
    return Point<T>(a) += b;
}

template <Arithmetic T>
constexpr Point<T> operator-(const Point<T>& a, const Point<T>& b) {
    return Point<T>(a) -= b;
}

template <Arithmetic T>
constexpr Point<T> operator-(const Point<T>& p) {
    return Point<T>{-p.x, -p.y};
}

template <Arithmetic T>
constexpr Point<T> operator*(const Point<T>& p, T scale) {
    return Point<T>(p) *= scale;
}

template <Arithmetic T>
constexpr Point<T> operator*(T scale, const Point<T>& p) {
    return p * scale;
}

template <Arithmetic T>
constexpr Point<T> operator/(const Point<T>& p, T scale) {
    return Point<T>(p) /= scale;
}

using PointI = Point<int>;
using PointF = Point<float>;
using PointD = Point<double>;

} // namespace visor::base
