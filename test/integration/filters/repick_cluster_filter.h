#ifndef THIRD_PARTY_ENVOY_SRC_TEST_INTEGRATION_FILTERS_REPICK_CLUSTER_FILTER_H_
#define THIRD_PARTY_ENVOY_SRC_TEST_INTEGRATION_FILTERS_REPICK_CLUSTER_FILTER_H_

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RepickClusterFilter {

// The cluster names and cluster headers here need to be correlated correctly with
// weighted_cluster_integration_test or any other end users, to make sure that header modifications
// have been done on the correct target. So they are declared and defined here.
inline constexpr absl::string_view ClusterName = "cluster_%d";
inline constexpr absl::string_view ClusterHeaderName = "cluster_header_%d";
// Currently, for the simplicity of testing purpose:
// [0, TotalUpstreamClusterCount - TotalUpstreamClusterWithHeaderCount) will be
// clusters with `name` field specified.
// [TotalUpstreamClusterCount - TotalUpstreamClusterWithHeaderCount, TotalUpstreamClusterCount)
// will be clusters with `cluster_header` field specified.
inline constexpr int TotalUpstreamClusterCount = 2;
inline constexpr int TotalUpstreamClusterWithHeaderCount = 1;

} // namespace RepickClusterFilter
} // namespace Envoy
#endif // THIRD_PARTY_ENVOY_SRC_TEST_INTEGRATION_FILTERS_REPICK_CLUSTER_FILTER_H_
