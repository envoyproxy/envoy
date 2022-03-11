#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RepickClusterFilter {

// The cluster names and cluster headers here need to be correlated correctly with
// weighted_cluster_integration_test or any other end users, to make sure that header modifications
// have been done on the correct target. So they are declared and defined here.
inline constexpr absl::string_view ClusterName = "cluster_1";
inline constexpr absl::string_view ClusterHeaderName = "cluster_header_1";

} // namespace RepickClusterFilter
} // namespace Envoy
