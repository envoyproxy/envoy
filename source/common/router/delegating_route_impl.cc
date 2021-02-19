#include "common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

const DirectResponseEntry* DelegatingRoute::directResponseEntry() const {
  return base_route_->directResponseEntry();
}

const RouteEntry* DelegatingRoute::routeEntry() const { return base_route_->routeEntry(); }

const Decorator* DelegatingRoute::decorator() const { return base_route_->decorator(); }

const RouteTracing* DelegatingRoute::tracingConfig() const { return base_route_->tracingConfig(); }

const RouteSpecificFilterConfig* DelegatingRoute::perFilterConfig(const std::string& name) const {
  return base_route_->perFilterConfig(name);
}

} // namespace Router
} // namespace Envoy
