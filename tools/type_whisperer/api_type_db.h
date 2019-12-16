#pragma once

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Tools {
namespace TypeWhisperer {

// We don't expose the raw API type database to consumers, as this requires RTTI
// and this may be linked in environments where this is not available (e.g.
// libtooling binaries).
class ApiTypeDb {
public:
  static absl::optional<std::string> getProtoPathForType(const std::string& type_name);
};

} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
