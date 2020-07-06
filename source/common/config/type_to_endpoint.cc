#include "common/config/type_to_endpoint.h"

#include "envoy/annotations/resource.pb.h"

#include "common/grpc/common.h"

// API_NO_BOOST_FILE

#define API_VERSION envoy::config::core::v3::ApiVersion
#define SERVICE_VERSION_INFO(v2, v3)                                                               \
  createServiceVersionInfoMap(v2, API_VERSION::V2, {v2, v3}),                                      \
      createServiceVersionInfoMap(v3, API_VERSION::V3, {v2, v3})

namespace Envoy {
namespace Config {

namespace {

// A service's name, e.g. "envoy.api.v2.RouteDiscoveryService",
// "envoy.service.route.v3.RouteDiscoveryService".
using ServiceName = std::string;

struct ServiceVersionInfo {
  // The transport_api_version of a service. Possible values:
  // envoy::config::core::v3::ApiVersion::V2, envoy::config::core::v3::ApiVersion::V3.
  envoy::config::core::v3::ApiVersion transport_api_version_;

  // This hold a name for each transport_api_version, for example for
  // "envoy.api.v2.RouteDiscoveryService".
  //
  // {
  //    "V2": "envoy.api.v2.RouteDiscoveryService",
  //    "V3": "envoy.service.route.v3.RouteDiscoveryService"
  // }
  std::unordered_map<envoy::config::core::v3::ApiVersion, ServiceName> names_;
};

// A ServiceVersionInfoMap holds a service's transport_api_version and possible names for each
// available transport_api_version. For examples:
//
// Given "envoy.api.v2.RouteDiscoveryService" as the service name:
// {
//   "envoy.api.v2.RouteDiscoveryService": {
//     "transport_api_version_": "V2",
//     "names_": {
//       "V2": "envoy.api.v2.RouteDiscoveryService",
//       "V3": "envoy.service.route.v3.RouteDiscoveryService"
//     }
//   }
// }
//
// And for "envoy.service.route.v3.RouteDiscoveryService":
// {
//   "envoy.service.route.v3.RouteDiscoveryService": {
//     "transport_api_version_": "V3",
//     "names_": {
//       "V2": "envoy.api.v2.RouteDiscoveryService",
//       "V3": "envoy.service.route.v3.RouteDiscoveryService"
//     }
//   }
// }
using ServiceVersionInfoMap = std::unordered_map<ServiceName, ServiceVersionInfo>;

// This creates a ServiceVersionInfoMap, with service name (For example:
// "envoy.api.v2.RouteDiscoveryService") as the key.
ServiceVersionInfoMap
createServiceVersionInfoMap(absl::string_view service_name,
                            envoy::config::core::v3::ApiVersion transport_api_version,
                            const std::array<std::string, 2>& versioned_service_names) {
  const auto key = static_cast<ServiceName>(service_name);
  return ServiceVersionInfoMap{{
      // ServiceName as the key.
      key,

      // ServiceVersionInfo as the value.
      ServiceVersionInfo{
          transport_api_version,
          {
              {envoy::config::core::v3::ApiVersion::V2, versioned_service_names[0]},
              {envoy::config::core::v3::ApiVersion::V3, versioned_service_names[1]},
          },
      },
  }};
}

// A method name, e.g. "envoy.api.v2.RouteDiscoveryService.StreamRoutes".
using MethodName = std::string;

// A resource type URL. For example: "type.googleapis.com/envoy.api.v2.RouteConfiguration".
using TypeUrl = std::string;

using TypeUrlVersionMap = std::unordered_map<TypeUrl, envoy::config::core::v3::ApiVersion>;
using VersionedMethodMap = std::unordered_map<envoy::config::core::v3::ApiVersion, MethodName>;

struct VersionedDiscoveryType {
  // This holds a map of type url to its corresponding transport_api_version. For example:
  // {
  //   "type.googleapis.com/envoy.api.v2.RouteConfiguration": "V2",
  //   "type.googleapis.com/envoy.config.route.v3.RouteConfiguration": "V3"
  // }
  TypeUrlVersionMap type_url_versions_;

  // Versioned (by transport_api_version) discovery service RPC method fully qualified names. e.g.
  // {
  //   "V2": "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
  //   "V3": "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes"
  // }
  VersionedMethodMap methods_;
};

TypeUrl getResourceTypeUrl(absl::string_view service_name) {
  const auto* service_desc = Protobuf::DescriptorPool::generated_pool()->FindServiceByName(
      static_cast<ServiceName>(service_name));
  ASSERT(service_desc != nullptr, fmt::format("{} missing", service_name));
  ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));

  return Grpc::Common::typeUrl(
      service_desc->options().GetExtension(envoy::annotations::resource).type());
}

struct VersionedService {
  VersionedDiscoveryType sotw_grpc_;
  VersionedDiscoveryType delta_grpc_;
  VersionedDiscoveryType rest_;
};

using TypeUrlToVersionedServiceMap = std::unordered_map<TypeUrl, VersionedService>;

// buildTypeUrlToServiceMap() builds a reverse map from a resource type URLs to a versioned service
// (by transport_api_version).
//
// The way we build it is by firstly constructing a list of ServiceVersionInfoMap:
// [
//   {
//     "envoy.api.v2.RouteDiscoveryService": {
//       "version_": "V2",
//       "names_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService"
//       }
//     }
//   },
//   {
//     "envoy.service.route.v3.RouteDiscoveryService": {
//       "version_": "V3",
//       "names_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService"
//       }
//     }
//   }
//  ...
// ]
//
// Then we convert it into the following map, with the inferred resource type URL as the key:
//
// {
//   "type.googleapis.com/envoy.api.v2.RouteConfiguration": {
//     "sotw_grpc_": {
//       "type_url_versions_": {
//         "type.googleapis.com/envoy.api.v2.RouteConfiguration": "V2",
//         "type.googleapis.com/envoy.config.route.v3.RouteConfiguration": "V3"
//       },
//       "methods_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes"
//       }
//     },
//     ...
//   },
//   "type.googleapis.com/envoy.config.route.v3.RouteConfiguration": {
//     "sotw_grpc_": {
//       "type_url_versions_": {
//         "type.googleapis.com/envoy.api.v2.RouteConfiguration": "V2",
//         "type.googleapis.com/envoy.config.route.v3.RouteConfiguration": "V3"
//       },
//       "methods_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes"
//       }
//     },
//     ...
//   }
// }
//
// This lookup map provides a quick access to get the effective transport_api_version when the
// transport_api_version input is "AUTO", since the mapping to get the actual version is available
// in each entry.
TypeUrlToVersionedServiceMap* buildTypeUrlToServiceMap() {
  auto* type_url_to_versioned_service_map = new TypeUrlToVersionedServiceMap();

  // This happens once in the lifetime of Envoy. We build a reverse map from resource type URL to
  // service methods (versioned by transport_api_version). We explicitly enumerate all services,
  // since DescriptorPool doesn't support iterating over all descriptors, due its lazy load design,
  // see https://www.mail-archive.com/protobuf@googlegroups.com/msg04540.html.
  for (const ServiceVersionInfoMap& registered : {
           SERVICE_VERSION_INFO("envoy.api.v2.RouteDiscoveryService",
                                "envoy.service.route.v3.RouteDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.api.v2.ScopedRoutesDiscoveryService",
                                "envoy.service.route.v3.ScopedRoutesDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.api.v2.ScopedRoutesDiscoveryService",
                                "envoy.service.route.v3.ScopedRoutesDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.api.v2.VirtualHostDiscoveryService",
                                "envoy.service.route.v3.VirtualHostDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.service.discovery.v2.SecretDiscoveryService",
                                "envoy.service.secret.v3.SecretDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.api.v2.ClusterDiscoveryService",
                                "envoy.service.cluster.v3.ClusterDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.api.v2.EndpointDiscoveryService",
                                "envoy.service.endpoint.v3.EndpointDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.api.v2.ListenerDiscoveryService",
                                "envoy.service.listener.v3.ListenerDiscoveryService"),
           SERVICE_VERSION_INFO("envoy.service.discovery.v2.RuntimeDiscoveryService",
                                "envoy.service.runtime.v3.RuntimeDiscoveryService"),
       }) {
    for (const auto& registered_service : registered) {
      const TypeUrl resource_type_url = getResourceTypeUrl(registered_service.first);
      VersionedService& service = (*type_url_to_versioned_service_map)[resource_type_url];

      for (const auto& versioned_service_name : registered_service.second.names_) {
        const ServiceName& service_name = versioned_service_name.second;
        const auto* service_desc =
            Protobuf::DescriptorPool::generated_pool()->FindServiceByName(service_name);
        ASSERT(service_desc != nullptr, fmt::format("{} missing", service_name));
        ASSERT(service_desc->options().HasExtension(envoy::annotations::resource));

        // We populate the service methods that are known below, but it's possible that some
        // services don't implement all, e.g. VHDS doesn't support SotW or REST.
        const auto current_type_url_version = registered_service.second.transport_api_version_;
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

envoy::config::core::v3::ApiVersion
effectiveTransportApiVersion(absl::string_view type_url,
                             envoy::config::core::v3::ApiVersion transport_api_version,
                             const TypeUrlVersionMap& version_map) {
  // By default (when the transport_api_version is "AUTO"), the effective transport_api_version is
  // the same as the version inferred from type_url.
  if (transport_api_version == envoy::config::core::v3::ApiVersion::AUTO) {
    const auto it = version_map.find(static_cast<TypeUrl>(type_url));
    ASSERT(it != version_map.cend());
    return it->second;
  }
  return transport_api_version;
}

} // namespace

const Protobuf::MethodDescriptor&
deltaGrpcMethod(absl::string_view type_url,
                envoy::config::core::v3::ApiVersion transport_api_version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      // The list of possible transport_api_versions for a type_url is provided in
      // .type_url_versions_.
      it->second.delta_grpc_.methods_[effectiveTransportApiVersion(
          type_url, transport_api_version, it->second.delta_grpc_.type_url_versions_)]);
}

const Protobuf::MethodDescriptor&
sotwGrpcMethod(absl::string_view type_url,
               envoy::config::core::v3::ApiVersion transport_api_version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.sotw_grpc_.methods_[effectiveTransportApiVersion(
          type_url, transport_api_version, it->second.sotw_grpc_.type_url_versions_)]);
}

const Protobuf::MethodDescriptor&
restMethod(absl::string_view type_url, envoy::config::core::v3::ApiVersion transport_api_version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.rest_.methods_[effectiveTransportApiVersion(type_url, transport_api_version,
                                                             it->second.rest_.type_url_versions_)]);
}

} // namespace Config
} // namespace Envoy
