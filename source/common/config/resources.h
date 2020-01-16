#pragma once

#include <string>

#include "envoy/config/core/v3alpha/config_source.pb.h"

#include "common/common/assert.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Config {

/**
 * Constant Type URLs.
 */
class TypeUrlValues {
public:
  const std::string Listener{"type.googleapis.com/envoy.api.v2.Listener"};
  const std::string Cluster{"type.googleapis.com/envoy.api.v2.Cluster"};
  const std::string ClusterLoadAssignment{"type.googleapis.com/envoy.api.v2.ClusterLoadAssignment"};
  const std::string Secret{"type.googleapis.com/envoy.api.v2.auth.Secret"};
  const std::string RouteConfiguration{"type.googleapis.com/envoy.api.v2.RouteConfiguration"};
  const std::string VirtualHost{"type.googleapis.com/envoy.api.v2.route.VirtualHost"};
  const std::string ScopedRouteConfiguration{
      "type.googleapis.com/envoy.api.v2.ScopedRouteConfiguration"};
  const std::string Runtime{"type.googleapis.com/envoy.service.discovery.v2.Runtime"};

  // TODO(shikugawa): integrate with resource name loader
  const std::string ListenerV3Alpha{"type.googleapis.com/envoy.config.listener.v3alpha.Listener"};
  const std::string ClusterV3Alpha{"type.googleapis.com/envoy.config.cluster.v3alpha.Cluster"};
  const std::string RouteConfigurationV3Alpha{
      "type.googleapis.com/envoy.config.route.v3alpha.RouteConfiguration"};
  const std::string VirtualHostV3Alpha{
      "type.googleapis.com/envoy.config.route.v3alpha.VirtualHost"};
  const std::string ScopedRouteConfigurationV3Alpha{
      "type.googleapis.com/envoy.config.route.v3alpha.ScopedRouteConfiguration"};
  const std::string RuntimeV3Alpha{"type.googleapis.com/envoy.service.runtime.v3alpha.Runtime"};
  const std::string SecretV3Alpha{
      "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3alpha.Secret"};
  const std::string ClusterLoadAssignmentV3Alpha{
      "type.googleapis.com/envoy.config.endpoint.v3alpha.ClusterLoadAssignment"};

  // TODO(shikugawa): integrate with resource name loader
  std::string fallback(const std::string& type_url) const {
    if (type_url == SecretV3Alpha) {
      return Secret;
    } else if (type_url == ClusterLoadAssignmentV3Alpha) {
      return ClusterLoadAssignment;
    } else if (type_url == ListenerV3Alpha) {
      return Listener;
    } else if (type_url == ClusterV3Alpha) {
      return Cluster;
    } else if (type_url == RouteConfigurationV3Alpha) {
      return RouteConfiguration;
    } else if (type_url == VirtualHostV3Alpha) {
      return VirtualHost;
    } else if (type_url == ScopedRouteConfigurationV3Alpha) {
      return ScopedRouteConfiguration;
    } else if (type_url == RuntimeV3Alpha) {
      return Runtime;
    }
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  std::string
  apiVersionString(envoy::config::core::v3alpha::ApiVersion resource_api_version) const {
    switch (resource_api_version) {
    case envoy::config::core::v3alpha::ApiVersion::V2:
      return "v2";
    case envoy::config::core::v3alpha::ApiVersion::V3ALPHA:
      return "v3alpha";
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};

using TypeUrl = ConstSingleton<TypeUrlValues>;

} // namespace Config
} // namespace Envoy
