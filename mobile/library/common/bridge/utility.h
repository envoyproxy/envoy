#pragma once

#include <string>

#include "source/common/http/codes.h"

#include "library/common/data/utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Bridge {
namespace Utility {

envoy_error_code_t errorCodeFromLocalStatus(Http::Code status);

// Helper that converts a C++ map-like (i.e. anything that iterates over a pair of strings) type
// into an envoy_map, copying all the values.
template <class T> envoy_map makeEnvoyMap(const T& map) {
  envoy_map new_map;
  new_map.length = std::size(map);
  new_map.entries = new envoy_map_entry[std::size(map)];

  uint64_t i = 0;
  for (const auto& e : map) {
    new_map.entries[i].key = Envoy::Data::Utility::copyToBridgeData(e.first);
    new_map.entries[i].value = Envoy::Data::Utility::copyToBridgeData(e.second);
    i++;
  }

  return new_map;
}

// Function overload that helps resolve makeEnvoyMap({{"key", "value"}}).
envoy_map makeEnvoyMap(std::initializer_list<std::pair<std::string, std::string>> map);

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
