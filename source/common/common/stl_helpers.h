#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

#include "absl/strings/str_join.h"

namespace Envoy {
/**
 * See if a reference exists within a container of std::reference_wrappers.
 */
template <class Container, class T> bool containsReference(const Container& c, const T& t) {
  return std::find_if(c.begin(), c.end(), [&](std::reference_wrapper<T> e) -> bool {
           return &e.get() == &t;
         }) != c.end();
}
} // namespace Envoy

// NOLINT(namespace-envoy)
// Overload functions in std library.
namespace std {
// Overload std::operator<< to output a vector.
template <class T> std::ostream& operator<<(std::ostream& out, const std::vector<T>& v) {
  out << "vector { " << absl::StrJoin(v, ", ", absl::StreamFormatter()) << " }";
  return out;
}

} // namespace std
