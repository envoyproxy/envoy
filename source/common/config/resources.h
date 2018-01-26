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
  const std::string Listener{"type.googleapis.com/envoy.api.v2.listener.Listener"};
  const std::string Cluster{"type.googleapis.com/envoy.api.v2.cluster.Cluster"};
  const std::string ClusterLoadAssignment{
      "type.googleapis.com/envoy.service.discovery.v2.ClusterLoadAssignment"};
  const std::string RouteConfiguration{"type.googleapis.com/envoy.api.v2.route.RouteConfiguration"};
};

typedef ConstSingleton<TypeUrlValues> TypeUrl;

} // namespace Config
} // namespace Envoy
