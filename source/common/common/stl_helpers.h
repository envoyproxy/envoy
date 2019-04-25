#pragma once

#include <algorithm>
#include <functional>

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
