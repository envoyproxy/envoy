#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/service/accesslog/v2/als.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/service/metrics/v2/metrics_service.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"

#include "common/common/fmt.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {

// This function verifies that critical descriptors have not changed name. It includes both gRPC
// services as well as types that are referenced in Any messages. 
bool validateProtoDescriptors() {
  
  bool descriptors_valid = true;

  // Hack to force linking of the service: https://github.com/google/protobuf/issues/4221
  // TODO(kuat): Remove explicit proto descriptor import.
  envoy::service::discovery::v2::AdsDummy dummy;
  envoy::service::ratelimit::v2::RateLimitRequest rls_dummy;

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
      "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs",
      "envoy.service.metrics.v2.MetricsService.StreamMetrics",
      "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit",
  };

  for (const auto& method : methods) {
    if (Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) == nullptr) {
        descriptors_valid = false;
        break;
    }
  }

  const auto types = {
      "envoy.api.v2.Cluster",
      "envoy.api.v2.ClusterLoadAssignment",
      "envoy.api.v2.Listener",
      "envoy.api.v2.RouteConfiguration",
  };

  for (const auto& type : types) {
    if (Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type) == nullptr) {
       descriptors_valid = false;
    }
  }
  return descriptors_valid;
};
} // namespace Envoy
