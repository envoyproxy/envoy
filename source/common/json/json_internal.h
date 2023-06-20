#pragma once

#include <list>
#include <string>

#include "envoy/json/json_object.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Nlohmann {

class Factory {
public:
  /**
   * Constructs a Json Object from a string.
   */
  static ObjectSharedPtr loadFromString(const std::string& json);

  /**
   * Serializes a string in JSON format, throwing an exception if not valid UTF-8.
   *
   * @param The raw string -- must be in UTF-8 format.
   * @return A string suitable for inclusion in a JSON stream, including double-quotes.
   */
  static std::string serialize(absl::string_view str);
};

} // namespace Nlohmann
} // namespace Json
} // namespace Envoy
