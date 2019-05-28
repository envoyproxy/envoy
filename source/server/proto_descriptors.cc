#include "server/proto_descriptors.h"

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/protobuf_link_hacks.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {

void validateProtoDescriptors() {
  const auto methods = {
      "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
      "envoy.api.v2.ClusterDiscoveryService.StreamClusters",
      "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints",
      "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints",
      "envoy.api.v2.ListenerDiscoveryService.FetchListeners",
      "envoy.api.v2.ListenerDiscoveryService.StreamListeners",
      "envoy.api.v2.RouteDiscoveryService.FetchRoutes",
      "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
      "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources",
      "envoy.service.discovery.v2.HealthDiscoveryService.FetchHealthCheck",
      "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck",
      "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit",
  };

  for (const auto& method : methods) {
    RELEASE_ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) != nullptr,
                   "");
  }

  const auto types = {
      "envoy.api.v2.Cluster",
      "envoy.api.v2.ClusterLoadAssignment",
      "envoy.api.v2.Listener",
      "envoy.api.v2.RouteConfiguration",
  };

  for (const auto& type : types) {
    RELEASE_ASSERT(
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type) != nullptr, "");
  }
};
} // namespace Server
} // namespace Envoy
