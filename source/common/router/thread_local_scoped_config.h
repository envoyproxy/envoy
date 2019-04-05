#pragma once

#include <memory>
#include <string>

#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Router {

// TODO(AndresGuedez):
class ScopedRouteInfo {
public:
  ScopedRouteInfo(const envoy::api::v2::ScopedRouteConfiguration& config_proto)
      : config_proto_(config_proto) {}

  const envoy::api::v2::ScopedRouteConfiguration config_proto_;
};
using ScopedRouteInfoConstSharedPtr = std::shared_ptr<const ScopedRouteInfo>;

// TODO(AndresGuedez):
class ThreadLocalScopedConfig : public ThreadLocal::ThreadLocalObject {
public:
  virtual ~ThreadLocalScopedConfig() {}

  // Adds/updates a routing scope specified via the Scoped RDS API. This scope will be added to the
  // set of scopes matched against the scope keys built for each HTTP request.
  virtual void addOrUpdateRoutingScope(ScopedRouteInfoConstSharedPtr scoped_route_info) PURE;

  // Removes a routing scope from the set of scopes matched against each HTTP request.
  virtual void removeRoutingScope(const std::string& scope_name) PURE;
};

} // namespace Router
} // namespace Envoy
