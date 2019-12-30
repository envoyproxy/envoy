#include "common/config/type_to_endpoint.h"

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/api/v2/srds.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"

#include "common/common/assert.h"
#include "common/grpc/common.h"

// API_NO_BOOST_FILE

namespace Envoy {
namespace Config {

const char UnknownMethod[] = "could_not_lookup_method_due_to_unknown_type_url";

namespace {

bool typeUrlIs(absl::string_view type_url, const Protobuf::Message& msg) {
  return Grpc::Common::typeUrl(msg.GetDescriptor()->full_name()) == type_url;
}

std::string versionString(envoy::api::v2::core::ApiVersion transport_api_version) {
  switch (transport_api_version) {
  case envoy::api::v2::core::ApiVersion::AUTO:
  case envoy::api::v2::core::ApiVersion::V2:
    return "v2";
  case envoy::api::v2::core::ApiVersion::V3ALPHA:
    return "v3alpha";
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace

const Protobuf::MethodDescriptor&
deltaGrpcMethod(absl::string_view type_url,
                envoy::api::v2::core::ApiVersion transport_api_version) {
  std::string method_name = UnknownMethod;
  const std::string version = versionString(transport_api_version);
  if (typeUrlIs(type_url, envoy::api::v2::RouteConfiguration())) {
    method_name = "envoy.api." + version + ".RouteDiscoveryService.DeltaRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::ScopedRouteConfiguration())) {
    method_name = "envoy.api." + version + ".ScopedRoutesDiscoveryService.DeltaScopedRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::route::VirtualHost())) {
    method_name = "envoy.api." + version + ".VirtualHostDiscoveryService.DeltaVirtualHosts";
  } else if (typeUrlIs(type_url, envoy::api::v2::auth::Secret())) {
    method_name = "envoy.service.discovery." + version + ".SecretDiscoveryService.DeltaSecrets";
  } else if (typeUrlIs(type_url, envoy::api::v2::Cluster())) {
    method_name = "envoy.api." + version + ".ClusterDiscoveryService.DeltaClusters";
  } else if (typeUrlIs(type_url, envoy::api::v2::ClusterLoadAssignment())) {
    method_name = "envoy.api." + version + ".EndpointDiscoveryService.DeltaEndpoints";
  } else if (typeUrlIs(type_url, envoy::api::v2::Listener())) {
    method_name = "envoy.api." + version + ".ListenerDiscoveryService.DeltaListeners";
  } else if (typeUrlIs(type_url, envoy::service::discovery::v2::Runtime())) {
    method_name = "envoy.service.discovery." + version + ".RuntimeDiscoveryService.DeltaRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor&
sotwGrpcMethod(absl::string_view type_url, envoy::api::v2::core::ApiVersion transport_api_version) {
  std::string method_name = UnknownMethod;
  const std::string version = versionString(transport_api_version);
  if (typeUrlIs(type_url, envoy::api::v2::RouteConfiguration())) {
    method_name = "envoy.api." + version + ".RouteDiscoveryService.StreamRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::ScopedRouteConfiguration())) {
    method_name = "envoy.api." + version + ".ScopedRoutesDiscoveryService.StreamScopedRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::auth::Secret())) {
    method_name = "envoy.service.discovery." + version + ".SecretDiscoveryService.StreamSecrets";
  } else if (typeUrlIs(type_url, envoy::api::v2::Cluster())) {
    method_name = "envoy.api." + version + ".ClusterDiscoveryService.StreamClusters";
  } else if (typeUrlIs(type_url, envoy::api::v2::ClusterLoadAssignment())) {
    method_name = "envoy.api." + version + ".EndpointDiscoveryService.StreamEndpoints";
  } else if (typeUrlIs(type_url, envoy::api::v2::Listener())) {
    method_name = "envoy.api." + version + ".ListenerDiscoveryService.StreamListeners";
  } else if (typeUrlIs(type_url, envoy::service::discovery::v2::Runtime())) {
    method_name = "envoy.service.discovery." + version + ".RuntimeDiscoveryService.StreamRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor&
restMethod(absl::string_view type_url, envoy::api::v2::core::ApiVersion transport_api_version) {
  std::string method_name = UnknownMethod;
  const std::string version = versionString(transport_api_version);
  if (typeUrlIs(type_url, envoy::api::v2::RouteConfiguration())) {
    method_name = "envoy.api." + version + ".RouteDiscoveryService.FetchRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::ScopedRouteConfiguration())) {
    method_name = "envoy.api." + version + ".ScopedRoutesDiscoveryService.FetchScopedRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::auth::Secret())) {
    method_name = "envoy.service.discovery." + version + ".SecretDiscoveryService.FetchSecrets";
  } else if (typeUrlIs(type_url, envoy::api::v2::Cluster())) {
    method_name = "envoy.api." + version + ".ClusterDiscoveryService.FetchClusters";
  } else if (typeUrlIs(type_url, envoy::api::v2::ClusterLoadAssignment())) {
    method_name = "envoy.api." + version + ".EndpointDiscoveryService.FetchEndpoints";
  } else if (typeUrlIs(type_url, envoy::api::v2::Listener())) {
    method_name = "envoy.api." + version + ".ListenerDiscoveryService.FetchListeners";
  } else if (typeUrlIs(type_url, envoy::service::discovery::v2::Runtime())) {
    method_name = "envoy.service.discovery." + version + ".RuntimeDiscoveryService.FetchRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

} // namespace Config
} // namespace Envoy
