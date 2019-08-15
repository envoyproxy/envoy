#include "common/config/type_to_endpoint.h"

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/srds.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"

#include "common/grpc/common.h"

namespace Envoy {
namespace Config {

const char UnknownMethod[] = "could_not_lookup_method_due_to_unknown_type_url";

#define API_TYPE_URL_IS(x)                                                                         \
  (type_url == Grpc::Common::typeUrl(envoy::api::v2::x().GetDescriptor()->full_name()))
#define DISCOVERY_TYPE_URL_IS(x)                                                                   \
  (type_url ==                                                                                     \
   Grpc::Common::typeUrl(envoy::service::discovery::v2::x().GetDescriptor()->full_name()))
const Protobuf::MethodDescriptor& deltaGrpcMethod(absl::string_view type_url) {
  std::string method_name = UnknownMethod;
  if (API_TYPE_URL_IS(RouteConfiguration)) {
    method_name = "envoy.api.v2.RouteDiscoveryService.DeltaRoutes";
  } else if (API_TYPE_URL_IS(ScopedRouteConfiguration)) {
    method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.DeltaScopedRoutes";
  } else if (API_TYPE_URL_IS(route::VirtualHost)) {
    method_name = "envoy.api.v2.VirtualHostDiscoveryService.DeltaVirtualHosts";
  } else if (API_TYPE_URL_IS(auth::Secret)) {
    method_name = "envoy.service.discovery.v2.SecretDiscoveryService.DeltaSecrets";
  } else if (API_TYPE_URL_IS(Cluster)) {
    method_name = "envoy.api.v2.ClusterDiscoveryService.DeltaClusters";
  } else if (API_TYPE_URL_IS(ClusterLoadAssignment)) {
    method_name = "envoy.api.v2.EndpointDiscoveryService.DeltaEndpoints";
  } else if (API_TYPE_URL_IS(Listener)) {
    method_name = "envoy.api.v2.ListenerDiscoveryService.DeltaListeners";
  } else if (DISCOVERY_TYPE_URL_IS(Runtime)) {
    method_name = "envoy.service.discovery.v2.RuntimeDiscoveryService.DeltaRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor& sotwGrpcMethod(absl::string_view type_url) {
  std::string method_name = UnknownMethod;
  if (API_TYPE_URL_IS(RouteConfiguration)) {
    method_name = "envoy.api.v2.RouteDiscoveryService.StreamRoutes";
  } else if (API_TYPE_URL_IS(ScopedRouteConfiguration)) {
    method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.StreamScopedRoutes";
  } else if (API_TYPE_URL_IS(auth::Secret)) {
    method_name = "envoy.service.discovery.v2.SecretDiscoveryService.StreamSecrets";
  } else if (API_TYPE_URL_IS(Cluster)) {
    method_name = "envoy.api.v2.ClusterDiscoveryService.StreamClusters";
  } else if (API_TYPE_URL_IS(ClusterLoadAssignment)) {
    method_name = "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints";
  } else if (API_TYPE_URL_IS(Listener)) {
    method_name = "envoy.api.v2.ListenerDiscoveryService.StreamListeners";
  } else if (DISCOVERY_TYPE_URL_IS(Runtime)) {
    method_name = "envoy.service.discovery.v2.RuntimeDiscoveryService.StreamRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor& restMethod(absl::string_view type_url) {
  std::string method_name = UnknownMethod;
  if (API_TYPE_URL_IS(RouteConfiguration)) {
    method_name = "envoy.api.v2.RouteDiscoveryService.FetchRoutes";
  } else if (API_TYPE_URL_IS(ScopedRouteConfiguration)) {
    method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.FetchScopedRoutes";
  } else if (API_TYPE_URL_IS(auth::Secret)) {
    method_name = "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets";
  } else if (API_TYPE_URL_IS(Cluster)) {
    method_name = "envoy.api.v2.ClusterDiscoveryService.FetchClusters";
  } else if (API_TYPE_URL_IS(ClusterLoadAssignment)) {
    method_name = "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints";
  } else if (API_TYPE_URL_IS(Listener)) {
    method_name = "envoy.api.v2.ListenerDiscoveryService.FetchListeners";
  } else if (DISCOVERY_TYPE_URL_IS(Runtime)) {
    method_name = "envoy.service.discovery.v2.RuntimeDiscoveryService.FetchRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}
#undef API_TYPE_URL_IS

} // namespace Config
} // namespace Envoy
