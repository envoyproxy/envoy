#pragma once

#include <algorithm>
#include <functional>
#include <numeric>

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

/**
 * Accumulates a container of elements into a string of the format [string_func(element_0),
 * string_func(element_1), ...]
 */
template <class T, class ContainerT>
std::string accumulateToString(const ContainerT& source,
                               std::function<std::string(const T&)> string_func) {
  if (source.empty()) {
    return "[]";
  }
  return std::accumulate(std::next(source.begin()), source.end(),
                         "[" + string_func(*source.begin()),
                         [string_func](std::string acc, const T& element) {
                           return acc + ", " + string_func(element);
                         }) +
         "]";
}

// Used for converting sanctioned uses of std string_view (e.g. extensions) to absl::string_view
// for internal use.
inline absl::string_view toAbslStringView(std::string_view view) { // NOLINT(std::string_view)
  return {view.data(), view.size()};                               // NOLINT(std::string_view)
}

// Used for converting internal absl::string_view to sanctioned uses of std string_view (e.g.
// extensions).
inline std::string_view toStdStringView(absl::string_view view) { // NOLINT(std::string_view)
  return {view.data(), view.size()};                              // NOLINT(std::string_view)
}

} // namespace Envoy
