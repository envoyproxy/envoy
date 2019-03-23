#pragma once

#include <memory>
#include <string>

#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Router {

class ScopedRouteInfo {};
using ScopedRouteInfoConstSharedPtr = std::shared_ptr<const ScopedRouteInfo>;

class ThreadLocalScopedConfig : public ThreadLocal::ThreadLocalObject {
public:
  virtual ~ThreadLocalScopedConfig() {}

  virtual void addOrUpdateRoutingScope(ScopedRouteInfoConstSharedPtr scoped_route_info) PURE;

  // Removes a routing scope from the set of scopes matched against each HTTP request.
  virtual void removeRoutingScope(const std::string& scope_name) PURE;
};

} // namespace Router
} // namespace Envoy
