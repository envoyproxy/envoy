#pragma once

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

absl::Status parseImageURI(const std::string& uri, std::string& registry, std::string& image_name, std::string& tag);

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy