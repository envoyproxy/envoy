#pragma once

#include <sstream>

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// functions present in this header are used by request / response objects to print their fields
// nicely

// prints out std::vector
template <typename T> std::ostream& operator<<(std::ostream& os, const std::vector<T>& arg) {
  os << "[";
  for (auto iter = arg.begin(); iter != arg.end(); iter++) {
    if (iter != arg.begin()) {
      os << ", ";
    }
    os << *iter;
  }
  os << "]";
  return os;
}

// prints out absl::optional
template <typename T> std::ostream& operator<<(std::ostream& os, const absl::optional<T>& arg) {
  if (arg.has_value()) {
    os << *arg;
  } else {
    os << "<null>";
  }
  return os;
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
