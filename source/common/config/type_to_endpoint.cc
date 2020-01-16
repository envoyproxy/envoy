#include "common/config/type_to_endpoint.h"

#include "envoy/annotations/resource.pb.h"

#include "common/grpc/common.h"

// API_NO_BOOST_FILE

namespace Envoy {
namespace Config {

namespace {

// service RPC method fully qualified names.
struct Service {
  std::string sotw_grpc_method_;
  std::string delta_grpc_method_;
  std::string rest_method_;
};

// Map from resource type URL to service RPC methods.
using TypeUrlToServiceMap = std::unordered_map<std::string, Service>;

TypeUrlToServiceMap* buildTypeUrlToServiceMap() {
  auto* type_url_to_service_map = new TypeUrlToServiceMap();
  // This happens once in the lifetime of Envoy. We build a reverse map from resource type URL to
  // service methods. We explicitly enumerate all services, since DescriptorPool doesn't support
  // iterating over all descriptors, due its lazy load design, see
  // https://www.mail-archive.com/protobuf@googlegroups.com/msg04540.html.
  for (const std::string& service_name : {
           "envoy.api.v2.RouteDiscoveryService",
           "envoy.service.route.v3.RouteDiscoveryService",
           "envoy.api.v2.ScopedRoutesDiscoveryService",
           "envoy.service.route.v3.ScopedRoutesDiscoveryService",
           "envoy.api.v2.VirtualHostDiscoveryService",
           "envoy.service.route.v3.VirtualHostDiscoveryService",
           "envoy.service.discovery.v2.SecretDiscoveryService",
           "envoy.service.secret.v3.SecretDiscoveryService",
           "envoy.api.v2.ClusterDiscoveryService",
           "envoy.service.cluster.v3.ClusterDiscoveryService",
           "envoy.api.v2.EndpointDiscoveryService",
           "envoy.service.endpoint.v3.EndpointDiscoveryService",
           "envoy.api.v2.ListenerDiscoveryService",
           "envoy.service.listener.v3.ListenerDiscoveryService",
           "envoy.service.discovery.v2.RuntimeDiscoveryService",
           "envoy.service.runtime.v3.RuntimeDiscoveryService",
       }) {
    const auto* service_desc =
        Protobuf::DescriptorPool::generated_pool()->FindServiceByName(service_name);
    // TODO(htuch): this should become an ASSERT once all v3 descriptors are linked in.
    ASSERT(service_desc != nullptr, fmt::format("{} missing", service_name));
    ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));
    const std::string resource_type_url = Grpc::Common::typeUrl(
        service_desc->options().GetExtension(envoy::annotations::resource).type());
    Service& service = (*type_url_to_service_map)[resource_type_url];
    // We populate the service methods that are known below, but it's possible that some services
    // don't implement all, e.g. VHDS doesn't support SotW or REST.
    for (int method_index = 0; method_index < service_desc->method_count(); ++method_index) {
      const auto& method_desc = *service_desc->method(method_index);
      if (absl::StartsWith(method_desc.name(), "Stream")) {
        service.sotw_grpc_method_ = method_desc.full_name();
      } else if (absl::StartsWith(method_desc.name(), "Delta")) {
        service.delta_grpc_method_ = method_desc.full_name();
      } else if (absl::StartsWith(method_desc.name(), "Fetch")) {
        service.rest_method_ = method_desc.full_name();
      } else {
        ASSERT(false, "Unknown xDS service method");
      }
    }
  }
  return type_url_to_service_map;
}

TypeUrlToServiceMap& typeUrlToServiceMap() {
  static TypeUrlToServiceMap* type_url_to_service_map = buildTypeUrlToServiceMap();
  return *type_url_to_service_map;
}

} // namespace

const Protobuf::MethodDescriptor& deltaGrpcMethod(absl::string_view type_url) {
  const auto it = typeUrlToServiceMap().find(static_cast<std::string>(type_url));
  ASSERT(it != typeUrlToServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.delta_grpc_method_);
}

const Protobuf::MethodDescriptor& sotwGrpcMethod(absl::string_view type_url) {
  const auto it = typeUrlToServiceMap().find(static_cast<std::string>(type_url));
  ASSERT(it != typeUrlToServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.sotw_grpc_method_);
}

const Protobuf::MethodDescriptor& restMethod(absl::string_view type_url) {
  const auto it = typeUrlToServiceMap().find(static_cast<std::string>(type_url));
  ASSERT(it != typeUrlToServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(it->second.rest_method_);
}

} // namespace Config
} // namespace Envoy
