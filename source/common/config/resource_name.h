#pragma once

#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * Get resource name from api type.
 */
template <typename Current> std::string getResourceName() {
  return createReflectableMessage(Current())->GetDescriptor()->full_name();
}

/**
 * Get type url from api type.
 */
template <typename Current> std::string getTypeUrl() {
  return "type.googleapis.com/" + getResourceName<Current>();
}

} // namespace Config
} // namespace Envoy
