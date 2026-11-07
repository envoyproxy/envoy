#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

// Returns type URLs whose requests may be triggered while processing `type_url`.
// For example, RouteConfiguration updates may synchronously create VHDS subscriptions.
const std::vector<std::string>& dependentTypeUrls(absl::string_view type_url);

} // namespace Config
} // namespace Envoy
