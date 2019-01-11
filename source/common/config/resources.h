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
};

typedef ConstSingleton<TypeUrlValues> TypeUrl;

} // namespace Config
} // namespace Envoy
