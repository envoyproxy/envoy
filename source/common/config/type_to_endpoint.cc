#include "common/config/type_to_endpoint.h"

#include "envoy/annotations/resource.pb.h"

#include "common/grpc/common.h"

// API_NO_BOOST_FILE

#define SERVICE_VERSION_INFO(v2, v3)                                                               \
  createServiceVersionInfoMap(v2, {v2, v3}), createServiceVersionInfoMap(v3, {v2, v3})

namespace Envoy {
namespace Config {

namespace {

// A service's name, e.g. "envoy.api.v2.RouteDiscoveryService",
// "envoy.service.route.v3.RouteDiscoveryService".
using ServiceName = std::string;

struct ServiceVersionInfo {
  // This hold a name for each transport_api_version, for example for
  // "envoy.api.v2.RouteDiscoveryService":
  // {
  //    "V2": "envoy.api.v2.RouteDiscoveryService",
  //    "V3": "envoy.service.route.v3.RouteDiscoveryService"
  // }
  absl::flat_hash_map<envoy::config::core::v3::ApiVersion, ServiceName> names_;
};

// A ServiceVersionInfoMap holds a service's transport_api_version and possible names for each
// available transport_api_version. For examples:
//
// Given "envoy.api.v2.RouteDiscoveryService" as the service name:
// {
//   "envoy.api.v2.RouteDiscoveryService": {
//     "names_": {
//       "V2": "envoy.api.v2.RouteDiscoveryService",
//       "V3": "envoy.service.route.v3.RouteDiscoveryService"
//     }
//   }
// }
//
// And for "envoy.service.route.v3.RouteDiscoveryService":
// {
//   "envoy.service.route.v3.RouteDiscoveryService":
//     "names_": {
//       "V2": "envoy.api.v2.RouteDiscoveryService",
//       "V3": "envoy.service.route.v3.RouteDiscoveryService"
//     }
//   }
// }
using ServiceVersionInfoMap = absl::flat_hash_map<ServiceName, ServiceVersionInfo>;

// This creates a ServiceVersionInfoMap, with service name (For example:
// "envoy.api.v2.RouteDiscoveryService") as the key.
ServiceVersionInfoMap
createServiceVersionInfoMap(absl::string_view service_name,
                            const std::array<std::string, 2>& versioned_service_names) {
  const auto key = static_cast<ServiceName>(service_name);
  return ServiceVersionInfoMap{{
      // ServiceName as the key.
      key,

      // ServiceVersionInfo as the value.
      ServiceVersionInfo{{
          {envoy::config::core::v3::ApiVersion::V2, versioned_service_names[0]},
          {envoy::config::core::v3::ApiVersion::V3, versioned_service_names[1]},
      }},
  }};
}

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

struct VersionedDiscoveryType {
  // A map of transport_api_version to discovery service RPC method fully qualified names. e.g.
  // {
  //   "V2": "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
  //   "V3": "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes"
  // }
  absl::flat_hash_map<envoy::config::core::v3::ApiVersion, MethodName> methods_;
};

// This holds versioned discovery types.
struct VersionedService {
  VersionedDiscoveryType sotw_grpc_;
  VersionedDiscoveryType delta_grpc_;
  VersionedDiscoveryType rest_;
};

using TypeUrlToVersionedServiceMap = absl::flat_hash_map<TypeUrl, VersionedService>;

// buildTypeUrlToServiceMap() builds a reverse map from a resource type URLs to a versioned service
// (by transport_api_version).
//
// The way we build it is by firstly constructing a list of ServiceVersionInfoMap:
// [
//   {
//     "envoy.api.v2.RouteDiscoveryService": {
//       "names_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService"
//       }
//     }
//   },
//   {
//     "envoy.service.route.v3.RouteDiscoveryService": {
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
//       "methods_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes"
//       }
//     },
//     ...
//   },
//   "type.googleapis.com/envoy.config.route.v3.RouteConfiguration": {
//     "sotw_grpc_": {
//       "methods_": {
//         "V2": "envoy.api.v2.RouteDiscoveryService.StreamRoutes",
//         "V3": "envoy.service.route.v3.RouteDiscoveryService.StreamRoutes"
//       }
//     },
//     ...
//   }
// }
//
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
           ServiceVersionInfoMap{{
               "envoy.service.extension.v3.ExtensionConfigDiscoveryService",
               ServiceVersionInfo{{
                   {envoy::config::core::v3::ApiVersion::V3,
                    "envoy.service.extension.v3.ExtensionConfigDiscoveryService"},
               }},
           }},
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
        for (int method_index = 0; method_index < service_desc->method_count(); ++method_index) {
          const auto& method_desc = *service_desc->method(method_index);
          const auto transport_api_version = versioned_service_name.first;
          if (absl::StartsWith(method_desc.name(), "Stream")) {
            service.sotw_grpc_.methods_[transport_api_version] = method_desc.full_name();
          } else if (absl::StartsWith(method_desc.name(), "Delta")) {
            service.delta_grpc_.methods_[transport_api_version] = method_desc.full_name();
          } else if (absl::StartsWith(method_desc.name(), "Fetch")) {
            service.rest_.methods_[transport_api_version] = method_desc.full_name();
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
effectiveTransportApiVersion(envoy::config::core::v3::ApiVersion transport_api_version) {
  // By default (when the transport_api_version is "AUTO"), the effective transport_api_version is
  // envoy::config::core::v3::ApiVersion::V2.
  if (transport_api_version == envoy::config::core::v3::ApiVersion::AUTO) {
    return envoy::config::core::v3::ApiVersion::V2;
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
      it->second.delta_grpc_.methods_[effectiveTransportApiVersion(transport_api_version)]);
}

const Protobuf::MethodDescriptor&
sotwGrpcMethod(absl::string_view type_url,
               envoy::config::core::v3::ApiVersion transport_api_version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.sotw_grpc_.methods_[effectiveTransportApiVersion(transport_api_version)]);
}

const Protobuf::MethodDescriptor&
restMethod(absl::string_view type_url, envoy::config::core::v3::ApiVersion transport_api_version) {
  const auto it = typeUrlToVersionedServiceMap().find(static_cast<TypeUrl>(type_url));
  ASSERT(it != typeUrlToVersionedServiceMap().cend());
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      it->second.rest_.methods_[effectiveTransportApiVersion(transport_api_version)]);
}

} // namespace Config
} // namespace Envoy
