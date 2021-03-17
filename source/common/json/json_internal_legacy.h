#pragma once

#include <list>
#include <string>

#include "envoy/json/json_object.h"

namespace Envoy {
namespace Json {
namespace RapidJson {

class Factory {
public:
  /**
   * Constructs a Json Object from a string.
   */
  static ObjectSharedPtr loadFromString(const std::string& json);
};

} // namespace RapidJson
} // namespace Json
} // namespace Envoy
