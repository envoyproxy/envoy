#include "source/server/proto_descriptors.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

void validateProtoDescriptors() {
  const auto methods = {
      "envoy.service.cluster.v3.ClusterDiscoveryService.FetchClusters",
      "envoy.service.cluster.v3.ClusterDiscoveryService.StreamClusters",
      "envoy.service.cluster.v3.ClusterDiscoveryService.DeltaClusters",
      "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources",
      "envoy.service.discovery.v3.AggregatedDiscoveryService.DeltaAggregatedResources",
      "envoy.service.endpoint.v3.EndpointDiscoveryService.FetchEndpoints",
      "envoy.service.endpoint.v3.EndpointDiscoveryService.StreamEndpoints",
      "envoy.service.endpoint.v3.EndpointDiscoveryService.DeltaEndpoints",
      "envoy.service.endpoint.v3.LocalityEndpointDiscoveryService.DeltaLocalityEndpoints",
      "envoy.service.health.v3.HealthDiscoveryService.FetchHealthCheck",
      "envoy.service.health.v3.HealthDiscoveryService.StreamHealthCheck",
      "envoy.service.listener.v3.ListenerDiscoveryService.FetchListeners",
      "envoy.service.listener.v3.ListenerDiscoveryService.StreamListeners",
      "envoy.service.listener.v3.ListenerDiscoveryService.DeltaListeners",
      "envoy.service.ratelimit.v3.RateLimitService.ShouldRateLimit",
      "envoy.service.route.v3.RouteDiscoveryService.FetchRoutes",
      "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes",
      "envoy.service.route.v3.RouteDiscoveryService.DeltaRoutes",
      "envoy.service.runtime.v3.RuntimeDiscoveryService.StreamRuntime",
      "envoy.service.runtime.v3.RuntimeDiscoveryService.FetchRuntime",
  };

  for (const auto& method : methods) {
    RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                   absl::StrCat("Unable to find method descriptor for ", method));
  }

  const auto types = {
      "envoy.config.cluster.v3.Cluster",     "envoy.config.endpoint.v3.ClusterLoadAssignment",
      "envoy.config.listener.v3.Listener",   "envoy.config.route.v3.RouteConfiguration",
      "envoy.config.route.v3.VirtualHost",   "envoy.extensions.transport_sockets.tls.v3.Secret",
      "envoy.config.endpoint.v3.LbEndpoint",
  };

  for (const auto& type : types) {
    RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type) !=
                       nullptr,
                   absl::StrCat("Unable to find message type for ", type));
  }
}

} // namespace Server
} // namespace Envoy
