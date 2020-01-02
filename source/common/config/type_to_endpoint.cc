#include "common/config/type_to_endpoint.h"

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/rds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/api/v2/srds.pb.h"
#include "envoy/api/v3alpha/auth/cert.pb.h"
#include "envoy/api/v3alpha/cds.pb.h"
#include "envoy/api/v3alpha/eds.pb.h"
#include "envoy/api/v3alpha/lds.pb.h"
#include "envoy/api/v3alpha/rds.pb.h"
#include "envoy/api/v3alpha/route/route.pb.h"
#include "envoy/service/discovery/v2/rtds.pb.h"
#include "envoy/service/discovery/v3alpha/rtds.pb.h"
#include "envoy/service/route/v3alpha/srds.pb.h"

#include "common/common/assert.h"
#include "common/grpc/common.h"

// API_NO_BOOST_FILE
// TODO(htuch): the cross product of transport, resource version and type is
// kind of crazy, we should use API annotations to link resources to their
// service methods. See https://github.com/envoyproxy/envoy/issues/9454.

namespace Envoy {
namespace Config {

const char UnknownMethod[] = "could_not_lookup_method_due_to_unknown_type_url";

namespace {

bool typeUrlIs(absl::string_view type_url, const Protobuf::Message& v2_msg,
               const Protobuf::Message& v3_msg) {
  return Grpc::Common::typeUrl(v2_msg.GetDescriptor()->full_name()) == type_url ||
         Grpc::Common::typeUrl(v3_msg.GetDescriptor()->full_name()) == type_url;
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
  if (typeUrlIs(type_url, envoy::api::v2::RouteConfiguration(),
                envoy::api::v3alpha::RouteConfiguration())) {
    method_name = "envoy.api." + version + ".RouteDiscoveryService.DeltaRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::ScopedRouteConfiguration(),
                       envoy::service::route::v3alpha::ScopedRouteConfiguration())) {
    if (version == "v2") {
      method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.DeltaScopedRoutes";
    } else {
      ASSERT(version == "v3alpha");
      method_name = "envoy.service.route.v3alpha.ScopedRoutesDiscoveryService.DeltaScopedRoutes";
    }
  } else if (typeUrlIs(type_url, envoy::api::v2::route::VirtualHost(),
                       envoy::api::v3alpha::route::VirtualHost())) {
    method_name = "envoy.api." + version + ".VirtualHostDiscoveryService.DeltaVirtualHosts";
  } else if (typeUrlIs(type_url, envoy::api::v2::auth::Secret(),
                       envoy::api::v3alpha::auth::Secret())) {
    method_name = "envoy.service.discovery." + version + ".SecretDiscoveryService.DeltaSecrets";
  } else if (typeUrlIs(type_url, envoy::api::v2::Cluster(), envoy::api::v3alpha::Cluster())) {
    method_name = "envoy.api." + version + ".ClusterDiscoveryService.DeltaClusters";
  } else if (typeUrlIs(type_url, envoy::api::v2::ClusterLoadAssignment(),
                       envoy::api::v3alpha::ClusterLoadAssignment())) {
    method_name = "envoy.api." + version + ".EndpointDiscoveryService.DeltaEndpoints";
  } else if (typeUrlIs(type_url, envoy::api::v2::Listener(), envoy::api::v3alpha::Listener())) {
    method_name = "envoy.api." + version + ".ListenerDiscoveryService.DeltaListeners";
  } else if (typeUrlIs(type_url, envoy::service::discovery::v2::Runtime(),
                       envoy::service::discovery::v3alpha::Runtime())) {
    method_name = "envoy.service.discovery." + version + ".RuntimeDiscoveryService.DeltaRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor&
sotwGrpcMethod(absl::string_view type_url, envoy::api::v2::core::ApiVersion transport_api_version) {
  std::string method_name = UnknownMethod;
  const std::string version = versionString(transport_api_version);
  if (typeUrlIs(type_url, envoy::api::v2::RouteConfiguration(),
                envoy::api::v3alpha::RouteConfiguration())) {
    method_name = "envoy.api." + version + ".RouteDiscoveryService.StreamRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::ScopedRouteConfiguration(),
                       envoy::service::route::v3alpha::ScopedRouteConfiguration())) {
    if (version == "v2") {
      method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.StreamScopedRoutes";
    } else {
      ASSERT(version == "v3alpha");
      method_name = "envoy.service.route.v3alpha.ScopedRoutesDiscoveryService.StreamScopedRoutes";
    }
  } else if (typeUrlIs(type_url, envoy::api::v2::auth::Secret(),
                       envoy::api::v3alpha::auth::Secret())) {
    method_name = "envoy.service.discovery." + version + ".SecretDiscoveryService.StreamSecrets";
  } else if (typeUrlIs(type_url, envoy::api::v2::Cluster(), envoy::api::v3alpha::Cluster())) {
    method_name = "envoy.api." + version + ".ClusterDiscoveryService.StreamClusters";
  } else if (typeUrlIs(type_url, envoy::api::v2::ClusterLoadAssignment(),
                       envoy::api::v3alpha::ClusterLoadAssignment())) {
    method_name = "envoy.api." + version + ".EndpointDiscoveryService.StreamEndpoints";
  } else if (typeUrlIs(type_url, envoy::api::v2::Listener(), envoy::api::v3alpha::Listener())) {
    method_name = "envoy.api." + version + ".ListenerDiscoveryService.StreamListeners";
  } else if (typeUrlIs(type_url, envoy::service::discovery::v2::Runtime(),
                       envoy::service::discovery::v3alpha::Runtime())) {
    method_name = "envoy.service.discovery." + version + ".RuntimeDiscoveryService.StreamRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

const Protobuf::MethodDescriptor&
restMethod(absl::string_view type_url, envoy::api::v2::core::ApiVersion transport_api_version) {
  std::string method_name = UnknownMethod;
  const std::string version = versionString(transport_api_version);
  if (typeUrlIs(type_url, envoy::api::v2::RouteConfiguration(),
                envoy::api::v3alpha::RouteConfiguration())) {
    method_name = "envoy.api." + version + ".RouteDiscoveryService.FetchRoutes";
  } else if (typeUrlIs(type_url, envoy::api::v2::ScopedRouteConfiguration(),
                       envoy::service::route::v3alpha::ScopedRouteConfiguration())) {
    if (version == "v2") {
      method_name = "envoy.api.v2.ScopedRoutesDiscoveryService.FetchScopedRoutes";
    } else {
      ASSERT(version == "v3alpha");
      method_name = "envoy.service.route.v3alpha.ScopedRoutesDiscoveryService.FetchScopedRoutes";
    }
  } else if (typeUrlIs(type_url, envoy::api::v2::auth::Secret(),
                       envoy::api::v3alpha::auth::Secret())) {
    method_name = "envoy.service.discovery." + version + ".SecretDiscoveryService.FetchSecrets";
  } else if (typeUrlIs(type_url, envoy::api::v2::Cluster(), envoy::api::v3alpha::Cluster())) {
    method_name = "envoy.api." + version + ".ClusterDiscoveryService.FetchClusters";
  } else if (typeUrlIs(type_url, envoy::api::v2::ClusterLoadAssignment(),
                       envoy::api::v3alpha::ClusterLoadAssignment())) {
    method_name = "envoy.api." + version + ".EndpointDiscoveryService.FetchEndpoints";
  } else if (typeUrlIs(type_url, envoy::api::v2::Listener(), envoy::api::v3alpha::Listener())) {
    method_name = "envoy.api." + version + ".ListenerDiscoveryService.FetchListeners";
  } else if (typeUrlIs(type_url, envoy::service::discovery::v2::Runtime(),
                       envoy::service::discovery::v3alpha::Runtime())) {
    method_name = "envoy.service.discovery." + version + ".RuntimeDiscoveryService.FetchRuntime";
  }
  ASSERT(method_name != UnknownMethod);
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
}

} // namespace Config
} // namespace Envoy
