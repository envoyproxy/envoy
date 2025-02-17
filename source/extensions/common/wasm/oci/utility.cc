#include "source/extensions/common/wasm/oci/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

absl::Status parseImageURI(const std::string& uri, std::string& registry, std::string& image_name, std::string& tag) {
  const std::string prefix = "oci://";
  if (!absl::StartsWith(uri, prefix)) {
    return {absl::StatusCode::kInvalidArgument, "Not OCI image URI"};
  }
  
  auto without_prefix = uri.substr(prefix.length());
  auto slash_pos = without_prefix.find('/');
  if (slash_pos == std::string::npos) {
    return {absl::StatusCode::kInvalidArgument, "URI does not include '/'"};
  }

  registry = without_prefix.substr(0, slash_pos);
  size_t colon_pos = without_prefix.find_last_of(':');
  if (colon_pos == std::string::npos || colon_pos < slash_pos + 1) {
    return {absl::StatusCode::kInvalidArgument, "URI does not include ':'"};
  }

  image_name = without_prefix.substr(slash_pos + 1, colon_pos - (slash_pos + 1));
  tag = without_prefix.substr(colon_pos + 1);

  return absl::OkStatus();
}

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy