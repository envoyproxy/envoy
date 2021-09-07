#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RepickClusterFilter {

// The cluster names and cluster headers here need to be correlated correctly with
// weighted_cluster_integration_test or any other end users, to make sure that header modifications
// have been done on the correct target. So they are declared and defined here.
inline constexpr absl::string_view ClusterNamePrefix = "cluster_%d";
inline constexpr absl::string_view ClusterHeaderNamePrefix = "cluster_header_%d";
// For the simplicity of testing purpose, the pattern of the cluster array is below:
// [0, TotalUpstreamClusterCount - TotalUpstreamClusterWithHeaderCount) will be
// clusters with `name` field specified.
// [TotalUpstreamClusterCount - TotalUpstreamClusterWithHeaderCount, TotalUpstreamClusterCount)
// will be clusters with `cluster_header` field specified.
inline constexpr int TotalUpstreamClusterCount = 2;
inline constexpr int TotalUpstreamClusterWithHeaderCount = 1;

} // namespace RepickClusterFilter
} // namespace Envoy
