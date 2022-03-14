#pragma once

#include <string>
#include <vector>

namespace Envoy {
namespace Config {

/**
 * Get resource name from api type.
 */
template <typename Current> std::string getResourceName() {
  return Current().GetDescriptor()->full_name();
}

/**
 * Get type url from api type.
 */
template <typename Current> std::string getTypeUrl() {
  return "type.googleapis.com/" + getResourceName<Current>();
}

} // namespace Config
} // namespace Envoy
