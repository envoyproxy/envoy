#pragma once

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Config {

// We don't expose the raw API type database to consumers, as this requires RTTI
// and this may be linked in environments where RTTI is not available (e.g.
// libtooling binaries).
class ApiTypeDb {
public:
  /**
   * Obtain the API directory relative path for the .proto for a given API type.
   * @param type_name fully qualified dot separated type name, e.g.
   *   envoy.type.Int64Range.
   * @return absl::optional<std::string> the corresponding proto path if found
   *   in the type DB, otherwise nullopt.
   */
  static absl::optional<std::string> getProtoPathForType(const std::string& type_name);
};

} // namespace Config
} // namespace Envoy
