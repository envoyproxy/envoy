#ifndef THIRD_PARTY_ENVOY_SRC_TEST_INTEGRATION_FILTERS_REPICK_CLUSTER_FILTER_H_
#define THIRD_PARTY_ENVOY_SRC_TEST_INTEGRATION_FILTERS_REPICK_CLUSTER_FILTER_H_

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RepickClusterFilter {

// TODO(tyxia) Consider improve the implementation here to make it untangled
// with `weighted_cluster_integration_test`. But the cluster names and cluster
// headers need to be correlated correctly between those two places, to make
// sure that header modifications have been done on the correct target.
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
