#include "common/config/type_to_endpoint.h"

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/srds.pb.h"

#include "common/grpc/common.h"

namespace Envoy {
namespace Config {

const char UnknownMethod[] = "could_not_lookup_method_due_to_unknown_type_url";

#define TYPE_URL_IS(x)                                                                             \
  (type_url == Grpc::Common::typeUrl(envoy::api::v2::x().GetDescriptor()->full_name()))
const Protobuf::MethodDescriptor& deltaGrpcMethod(absl::string_view type_url) {
  std::string method_name = UnknownMethod;
  if (TYPE_URL_IS(RouteConfiguration)) {
    method_name = "envoy.api.v2.RouteDiscoveryService.DeltaRoutes";
  } else if (TYPE_URL_IS(ScopedRouteConfiguration)) {
    method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.DeltaScopedRoutes";
  } else if (TYPE_URL_IS(route::VirtualHost)) {
    method_name = "envoy.api.v2.VirtualHostDiscoveryService.DeltaVirtualHosts";
  } else if (TYPE_URL_IS(auth::Secret)) {
    method_name = "envoy.service.discovery.v2.SecretDiscoveryService.DeltaSecrets";
  } else if (TYPE_URL_IS(Cluster)) {
    method_name = "envoy.api.v2.ClusterDiscoveryService.DeltaClusters";
  } else if (TYPE_URL_IS(ClusterLoadAssignment)) {
    method_name = "envoy.api.v2.EndpointDiscoveryService.DeltaEndpoints";
  } else if (TYPE_URL_IS(Listener)) {
    method_name = "envoy.api.v2.ListenerDiscoveryService.DeltaListeners";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor& sotwGrpcMethod(absl::string_view type_url) {
  std::string method_name = UnknownMethod;
  if (TYPE_URL_IS(RouteConfiguration)) {
    method_name = "envoy.api.v2.RouteDiscoveryService.StreamRoutes";
  } else if (TYPE_URL_IS(ScopedRouteConfiguration)) {
    method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.StreamScopedRoutes";
  } else if (TYPE_URL_IS(auth::Secret)) {
    method_name = "envoy.service.discovery.v2.SecretDiscoveryService.StreamSecrets";
  } else if (TYPE_URL_IS(Cluster)) {
    method_name = "envoy.api.v2.ClusterDiscoveryService.StreamClusters";
  } else if (TYPE_URL_IS(ClusterLoadAssignment)) {
    method_name = "envoy.api.v2.EndpointDiscoveryService.StreamEndpoints";
  } else if (TYPE_URL_IS(Listener)) {
    method_name = "envoy.api.v2.ListenerDiscoveryService.StreamListeners";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor& restMethod(absl::string_view type_url) {
  std::string method_name = UnknownMethod;
  if (TYPE_URL_IS(RouteConfiguration)) {
    method_name = "envoy.api.v2.RouteDiscoveryService.FetchRoutes";
  } else if (TYPE_URL_IS(ScopedRouteConfiguration)) {
    method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.FetchScopedRoutes";
  } else if (TYPE_URL_IS(auth::Secret)) {
    method_name = "envoy.service.discovery.v2.SecretDiscoveryService.FetchSecrets";
  } else if (TYPE_URL_IS(Cluster)) {
    method_name = "envoy.api.v2.ClusterDiscoveryService.FetchClusters";
  } else if (TYPE_URL_IS(ClusterLoadAssignment)) {
    method_name = "envoy.api.v2.EndpointDiscoveryService.FetchEndpoints";
  } else if (TYPE_URL_IS(Listener)) {
    method_name = "envoy.api.v2.ListenerDiscoveryService.FetchListeners";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}
#undef TYPE_URL_IS

} // namespace Config
} // namespace Envoy
