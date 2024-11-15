#include "source/common/config/type_to_endpoint.h"

#include "envoy/annotations/resource.pb.h"

#include "source/common/grpc/common.h"

// API_NO_BOOST_FILE

namespace Envoy {
namespace Config {

namespace {

// A service's name, e.g. "envoy.api.v2.RouteDiscoveryService",
// "envoy.service.route.v3.RouteDiscoveryService".
using ServiceName = std::string;

// A resource type URL. For example: "type.googleapis.com/envoy.api.v2.RouteConfiguration".
using TypeUrl = std::string;

TypeUrl getResourceTypeUrl(absl::string_view service_name) {
  const auto* service_desc = Protobuf::DescriptorPool::generated_pool()->FindServiceByName(
      static_cast<ServiceName>(service_name));
  ASSERT(service_desc != nullptr, fmt::format("{} missing", service_name));
  ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));

  return Grpc::Common::typeUrl(
      service_desc->options().GetExtension(envoy::annotations::resource).type());
}

// A method name, e.g. "envoy.api.v2.RouteDiscoveryService.StreamRoutes".
using MethodName = std::string;

// This holds  discovery types.
struct V3Service {
  MethodName sotw_grpc_;
  MethodName delta_grpc_;
  MethodName rest_;
};

using TypeUrlToV3ServiceMap = absl::flat_hash_map<TypeUrl, V3Service>;

TypeUrlToV3ServiceMap* buildTypeUrlToServiceMap() {
  auto* type_url_to_versioned_service_map = new TypeUrlToV3ServiceMap();

  // This happens once in the lifetime of Envoy. We build a reverse map from resource type URL to
  // service methods (versioned by transport_api_version). We explicitly enumerate all services,
  // since DescriptorPool doesn't support iterating over all descriptors, due its lazy load design,
  // see https://www.mail-archive.com/protobuf@googlegroups.com/msg04540.html.
  for (absl::string_view name : {
           "envoy.service.route.v3.RouteDiscoveryService",
           "envoy.service.route.v3.ScopedRoutesDiscoveryService",
           "envoy.service.route.v3.ScopedRoutesDiscoveryService",
           "envoy.service.route.v3.VirtualHostDiscoveryService",
           "envoy.service.secret.v3.SecretDiscoveryService",
           "envoy.service.cluster.v3.ClusterDiscoveryService",
           "envoy.service.endpoint.v3.EndpointDiscoveryService",
           "envoy.service.endpoint.v3.LocalityEndpointDiscoveryService",
           "envoy.service.listener.v3.ListenerDiscoveryService",
           "envoy.service.runtime.v3.RuntimeDiscoveryService",
           "envoy.service.extension.v3.ExtensionConfigDiscoveryService",
       }) {
    const TypeUrl resource_type_url = getResourceTypeUrl(name);
    V3Service& service = (*type_url_to_versioned_service_map)[resource_type_url];

    const auto* service_desc =
        Protobuf::DescriptorPool::generated_pool()->FindServiceByName(std::string(name));
    ASSERT(service_desc != nullptr, fmt::format("{} missing", name));
    ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));

    // We populate the service methods that are known below, but it's possible that some
    // services don't implement all, e.g. VHDS doesn't support SotW or REST.
    for (int method_index = 0; method_index < service_desc->method_count(); ++method_index) {
      const auto& method_desc = *service_desc->method(method_index);
      if (absl::StartsWith(method_desc.name(), "Stream")) {
        service.sotw_grpc_ = method_desc.full_name();
      } else if (absl::StartsWith(method_desc.name(), "Delta")) {
        service.delta_grpc_ = method_desc.full_name();
      } else if (absl::StartsWith(method_desc.name(), "Fetch")) {
        service.rest_ = method_desc.full_name();
      } else {
        ASSERT(false, "Unknown xDS service method");
      }
    }
  }
  return type_url_to_versioned_service_map;
}

TypeUrlToV3ServiceMap& typeUrlToV3ServiceMap() {
  static TypeUrlToV3ServiceMap* type_url_to_versioned_service_map = buildTypeUrlToServiceMap();
  return *type_url_to_versioned_service_map;
}

} // namespace

const Protobuf::MethodDescriptor& deltaGrpcMethod(absl::string_view type_url) {
  const auto it = typeUrlToV3ServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToV3ServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(it->second.delta_grpc_);
}

const Protobuf::MethodDescriptor& sotwGrpcMethod(absl::string_view type_url) {
  const auto it = typeUrlToV3ServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToV3ServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(it->second.sotw_grpc_);
}

const Protobuf::MethodDescriptor& restMethod(absl::string_view type_url) {
  const auto it = typeUrlToV3ServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToV3ServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(it->second.rest_);
}

} // namespace Config
} // namespace Envoy
