#pragma once

#include <string>

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
};

using TypeUrl = ConstSingleton<TypeUrlValues>;

} // namespace Config
} // namespace Envoy