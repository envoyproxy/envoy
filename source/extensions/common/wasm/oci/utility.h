#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

absl::Status parseImageURI(const std::string& uri, std::string& registry, std::string& image_name,
                           std::string& tag);

absl::StatusOr<std::string> prepareAuthorizationHeader(const std::string& image_pull_secret_raw,
                                                       const std::string& registry);

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
