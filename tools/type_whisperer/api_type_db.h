#pragma once

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Tools {
namespace TypeWhisperer {

// C++ representation of TypeDbDescription.
struct TypeInformation {
  TypeInformation(absl::string_view type_name, absl::string_view proto_path)
      : type_name_(type_name), proto_path_(proto_path) {}

  // Type's name in the next major version of the API.
  const std::string type_name_;

  // Path to .proto from API root.
  const std::string proto_path_;
};

// We don't expose the raw API type database to consumers, as this requires RTTI
// and this may be linked in environments where this is not available (e.g.
// libtool binaries).
class ApiTypeDb {
public:
  static absl::optional<TypeInformation> getLatestTypeInformation(const std::string& type_name);
};

} // namespace TypeWhisperer
} // namespace Tools
} // namespace Envoy
