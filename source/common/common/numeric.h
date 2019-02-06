#pragma once

#include <type_traits>

namespace Envoy {

// gcd() defined here is a substitute for C++17's implementation of std::gcd() from <numeric>,
// loosely adapted from http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4061.pdf.
// NOTE: std::abs() defined in <cmath> and <cstdlib> does not support arbitrary integral types
//       including, perhaps for obvious reasons, unsigned integers.
// TODO: When we move to C++17, please remove this implementation.

template <typename T> constexpr auto abs(T n) -> std::enable_if_t<std::is_unsigned<T>::value, T> {
  return n;
}

template <typename T>
constexpr auto abs(T n)
    -> std::enable_if_t<std::is_integral<T>::value and !std::is_unsigned<T>::value, T> {
  return n < T(0) ? -n : n;
}

template <typename T>
constexpr auto gcd(T m, T n) -> std::enable_if_t<std::is_integral<T>::value, T> {
  return n == 0 ? abs(m) : gcd(n, abs(m) % abs(n));
}

} // namespace Envoy
