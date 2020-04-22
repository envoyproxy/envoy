#include "server/proto_descriptors.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/protobuf_link_hacks.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

void validateProtoDescriptors() {
  const auto methods = {
      "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
      "envoy.api.v2.ClusterDiscoveryService.StreamClusters",
      "envoy.api.v2.ClusterDiscoveryService.DeltaClusters",
      "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
      "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints",
      "envoy.api.v2.EndpointDiscoveryService.DeltaEndpoints",
      "envoy.api.v2.ListenerDiscoveryService.FetchListeners",
      "envoy.api.v2.ListenerDiscoveryService.StreamListeners",
      "envoy.api.v2.ListenerDiscoveryService.DeltaListeners",
      "envoy.api.v2.RouteDiscoveryService.FetchRoutes",
      "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
      "envoy.api.v2.RouteDiscoveryService.DeltaRoutes",
      "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources",
      "envoy.service.discovery.v2.AggregatedDiscoveryService.DeltaAggregatedResources",
      "envoy.service.discovery.v2.HealthDiscoveryService.FetchHealthCheck",
      "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck",
      "envoy.service.discovery.v2.RuntimeDiscoveryService.FetchRuntime",
      "envoy.service.discovery.v2.RuntimeDiscoveryService.StreamRuntime",
      "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit",
  };

  for (const auto& method : methods) {
    RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                   absl::StrCat("Unable to find method descriptor for ", method));
  }

  const auto types = {
      "envoy.api.v2.Cluster",           "envoy.api.v2.ClusterLoadAssignment",
      "envoy.api.v2.Listener",          "envoy.api.v2.RouteConfiguration",
      "envoy.api.v2.route.VirtualHost", "envoy.api.v2.auth.Secret",
  };

  for (const auto& type : types) {
    RELEASE_ASSERT(
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type) != nullptr, "");
  }
}

} // namespace Server
} // namespace Envoy
