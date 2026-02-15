// NOLINT(namespace-envoy)
#include <cstdlib>
#include <iostream>

#include "envoy/api/v2/cds.grpc.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.grpc.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.grpc.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.grpc.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/service/accesslog/v2/als.grpc.pb.h"
#include "envoy/service/accesslog/v2/als.pb.h"
#include "envoy/service/discovery/v2/ads.grpc.pb.h"
#include "envoy/service/discovery/v2/ads.pb.h"
#include "envoy/service/discovery/v2/hds.grpc.pb.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/service/discovery/v2/rtds.grpc.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/metrics/v2/metrics_service.grpc.pb.h"
#include "envoy/service/metrics/v2/metrics_service.pb.h"
#include "envoy/service/ratelimit/v2/rls.grpc.pb.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"

#include "google/protobuf/descriptor.h"
#include "udpa/data/orca/v1/orca_load_report.pb.h"
#include "udpa/service/orca/v1/orca.grpc.pb.h"
#include "udpa/service/orca/v1/orca.pb.h"

// Workaround c++ protobuf issue: https://github.com/google/protobuf/issues/4221
// Force linking of proto descriptors by instantiating dummy message types.
const envoy::api::v2::CdsDummy _cds_dummy;
const envoy::api::v2::EdsDummy _eds_dummy;
const envoy::api::v2::LdsDummy _lds_dummy;
const envoy::api::v2::RdsDummy _rds_dummy;
const envoy::service::accesslog::v2::StreamAccessLogsMessage _als_dummy;
const envoy::service::discovery::v2::AdsDummy _ads_dummy;
const envoy::service::discovery::v2::Capability _hds_dummy;
const envoy::service::discovery::v2::RtdsDummy _rtds_dummy;
const envoy::service::metrics::v2::StreamMetricsMessage _metrics_dummy;
const envoy::service::ratelimit::v2::RateLimitRequest _rls_dummy;
// ORCA has no dummy message, so force-link via OrcaLoadReportRequest
const udpa::service::orca::v1::OrcaLoadReportRequest _orca_dummy;

// Basic C++ build/link validation for the v2 xDS APIs.
int main(int argc, char* argv[]) {
  // Force the ORCA gRPC service descriptor to be linked by referencing
  // the service_full_name()
  (void)udpa::service::orca::v1::OpenRcaService::service_full_name();

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
      "envoy.service.discovery.v2.RuntimeDiscoveryService.FetchRuntime",
      "envoy.service.discovery.v2.RuntimeDiscoveryService.StreamRuntime",
      "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs",
      "envoy.service.metrics.v2.MetricsService.StreamMetrics",
      "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit",
      "udpa.service.orca.v1.OpenRcaService.StreamCoreMetrics",
  };

  for (const auto& method : methods) {
    if (google::protobuf::DescriptorPool::generated_pool()->FindMethodByName(method) == nullptr) {
      std::cout << "Unable to find method descriptor for " << method << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  exit(EXIT_SUCCESS);
}
