#include "common/config/type_to_endpoint.h"

#include "envoy/annotations/resource.pb.h"

#include "common/grpc/common.h"

// API_NO_BOOST_FILE

#define API_VERSION envoy::config::core::v3::ApiVersion
#define VERSIONED(v2, v3)                                                                          \
  createServiceToVersionedMap(v2, API_VERSION::V2, {v2, v3}),                                      \
      createServiceToVersionedMap(v3, API_VERSION::V3, {v2, v3})

namespace Envoy {
namespace Config {

namespace {

struct VersionedMap {
  envoy::config::core::v3::ApiVersion version_;
  std::unordered_map<envoy::config::core::v3::ApiVersion, std::string> entries_;
};

// Map from resource type URL to versioned service RPC methods.
using ServiceToVersionedMap = std::unordered_map<std::string, VersionedMap>;
using TypeUrlVersionMap = std::unordered_map<std::string, envoy::config::core::v3::ApiVersion>;
using VersionedMethodMap = std::unordered_map<envoy::config::core::v3::ApiVersion, std::string>;

ServiceToVersionedMap
createServiceToVersionedMap(absl::string_view service, envoy::config::core::v3::ApiVersion version,
                            const std::array<std::string, 2>& versioned_services) {
  return {{static_cast<std::string>(service),
           VersionedMap{version,
                        {
                            {envoy::config::core::v3::ApiVersion::V2, versioned_services[0]},
                            {envoy::config::core::v3::ApiVersion::V3, versioned_services[1]},
                        }}}};
}

struct VersionedDiscoveryType {
  TypeUrlVersionMap type_url_versions_;

  // Versioned discovery service RPC method fully qualified names.
  VersionedMethodMap methods_;
};

// Versioned discovery service RPC service.
struct VersionedService {
  VersionedDiscoveryType sotw_grpc_;
  VersionedDiscoveryType delta_grpc_;
  VersionedDiscoveryType rest_;
};

using TypeUrlToVersionedServiceMap = std::unordered_map<std::string, VersionedService>;

std::string getResourceTypeUrl(absl::string_view service_name) {
  const auto* service_desc = Protobuf::DescriptorPool::generated_pool()->FindServiceByName(
      static_cast<std::string>(service_name));
  ASSERT(service_desc != nullptr, fmt::format("{} missing", service_name));
  ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));

  return Grpc::Common::typeUrl(
      service_desc->options().GetExtension(envoy::annotations::resource).type());
}

TypeUrlToVersionedServiceMap* buildTypeUrlToServiceMap() {
  auto* type_url_to_versioned_service_map = new TypeUrlToVersionedServiceMap();

  // This happens once in the lifetime of Envoy. We build a reverse map from resource type URL to
  // versioned service methods. We explicitly enumerate all services, since DescriptorPool doesn't
  // support iterating over all descriptors, due its lazy load design, see
  // https://www.mail-archive.com/protobuf@googlegroups.com/msg04540.html.
  for (const ServiceToVersionedMap& registered : {
           VERSIONED("envoy.api.v2.RouteDiscoveryService",
                     "envoy.service.route.v3.RouteDiscoveryService"),
           VERSIONED("envoy.api.v2.ScopedRoutesDiscoveryService",
                     "envoy.service.route.v3.ScopedRoutesDiscoveryService"),
           VERSIONED("envoy.api.v2.ScopedRoutesDiscoveryService",
                     "envoy.service.route.v3.ScopedRoutesDiscoveryService"),
           VERSIONED("envoy.api.v2.VirtualHostDiscoveryService",
                     "envoy.service.route.v3.VirtualHostDiscoveryService"),
           VERSIONED("envoy.service.discovery.v2.SecretDiscoveryService",
                     "envoy.service.secret.v3.SecretDiscoveryService"),
           VERSIONED("envoy.api.v2.ClusterDiscoveryService",
                     "envoy.service.cluster.v3.ClusterDiscoveryService"),
           VERSIONED("envoy.api.v2.EndpointDiscoveryService",
                     "envoy.service.endpoint.v3.EndpointDiscoveryService"),
           VERSIONED("envoy.api.v2.ListenerDiscoveryService",
                     "envoy.service.listener.v3.ListenerDiscoveryService"),
           VERSIONED("envoy.service.discovery.v2.RuntimeDiscoveryService",
                     "envoy.service.runtime.v3.RuntimeDiscoveryService"),
       }) {
    for (const auto& registered_service : registered) {
      const std::string resource_type_url = getResourceTypeUrl(registered_service.first);
      VersionedService& service = (*type_url_to_versioned_service_map)[resource_type_url];

      for (const auto& versioned_service_name : registered_service.second.entries_) {
        const auto& service_name = versioned_service_name.second;
        const auto* service_desc =
            Protobuf::DescriptorPool::generated_pool()->FindServiceByName(service_name);
        ASSERT(service_desc != nullptr, fmt::format("{} missing", service_name));
        ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));

        const auto current_type_url_version = registered_service.second.version_;
        for (int method_index = 0; method_index < service_desc->method_count(); ++method_index) {
          const auto& method_desc = *service_desc->method(method_index);
          if (absl::StartsWith(method_desc.name(), "Stream")) {
            service.sotw_grpc_.type_url_versions_[resource_type_url] = current_type_url_version;
            service.sotw_grpc_.methods_[versioned_service_name.first] = method_desc.full_name();
          } else if (absl::StartsWith(method_desc.name(), "Delta")) {
            service.delta_grpc_.type_url_versions_[resource_type_url] = current_type_url_version;
            service.delta_grpc_.methods_[versioned_service_name.first] = method_desc.full_name();
          } else if (absl::StartsWith(method_desc.name(), "Fetch")) {
            service.rest_.type_url_versions_[resource_type_url] = current_type_url_version;
            service.rest_.methods_[versioned_service_name.first] = method_desc.full_name();
          } else {
            ASSERT(false, "Unknown xDS service method");
          }
        }
      }
    }
  }
  return type_url_to_versioned_service_map;
}

TypeUrlToVersionedServiceMap& typeUrlToVersionedServiceMap() {
  static TypeUrlToVersionedServiceMap* type_url_to_versioned_service_map =
      buildTypeUrlToServiceMap();
  return *type_url_to_versioned_service_map;
}

envoy::config::core::v3::ApiVersion effectiveVersion(absl::string_view type_url,
                                                     envoy::config::core::v3::ApiVersion version,
                                                     const TypeUrlVersionMap& version_map) {
  // By default, the effective version is the same as the version inferred from type_url.
  if (version == envoy::config::core::v3::ApiVersion::AUTO) {
    const auto it = version_map.find(static_cast<std::string>(type_url));
    ASSERT(it != version_map.cend());
    return it->second;
  }
  return version;
}

} // namespace

const Protobuf::MethodDescriptor& deltaGrpcMethod(absl::string_view type_url,
                                                  envoy::config::core::v3::ApiVersion version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<std::string>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.delta_grpc_.methods_[effectiveVersion(type_url, version,
                                                       it->second.delta_grpc_.type_url_versions_)]);
}

const Protobuf::MethodDescriptor& sotwGrpcMethod(absl::string_view type_url,
                                                 envoy::config::core::v3::ApiVersion version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<std::string>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.sotw_grpc_
          .methods_[effectiveVersion(type_url, version, it->second.sotw_grpc_.type_url_versions_)]);
}

const Protobuf::MethodDescriptor& restMethod(absl::string_view type_url,
                                             envoy::config::core::v3::ApiVersion version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<std::string>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.rest_
          .methods_[effectiveVersion(type_url, version, it->second.rest_.type_url_versions_)]);
}

} // namespace Config
} // namespace Envoy
