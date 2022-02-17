#pragma once

#include <string>

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Config {

/**
 * Constant Type URLs.
 */
class TypeUrlValues {
public:
  const std::string Listener{"type.googleapis.com/envoy.config.listener.v3.Listener"};
  const std::string Cluster{"type.googleapis.com/envoy.config.cluster.v3.Cluster"};
  const std::string ClusterLoadAssignment{
      "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment"};
  const std::string LbEndpoint{"type.googleapis.com/envoy.config.endpoint.v3.LbEndpoint"};
  const std::string Secret{"type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"};
  const std::string RouteConfiguration{
      "type.googleapis.com/envoy.config.route.v3.RouteConfiguration"};
  const std::string VirtualHost{"type.googleapis.com/envoy.config.route.v3.VirtualHost"};
  const std::string ScopedRouteConfiguration{
      "type.googleapis.com/envoy.config.route.v3.ScopedRouteConfiguration"};
  const std::string Runtime{"type.googleapis.com/envoy.service.runtime.v3.Runtime"};
};

using TypeUrl = ConstSingleton<TypeUrlValues>;

} // namespace Config
} // namespace Envoy
