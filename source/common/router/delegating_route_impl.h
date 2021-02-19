#pragma once

#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of DelegatingRoute that delegates all method calls to the RouteConstSharedPtr
 * base route it wraps around.
 *
 * Intended to be used as a route mutability mechanism, where a filter can create a derived class of
 * DelegatingRoute and override specific methods (e.g. routeEntry) while preserving the rest of the
 * properties/behavior of the base route.
 */
class DelegatingRoute : public Router::Route {
public:
  DelegatingRoute(Router::RouteConstSharedPtr route) : base_route_(route) {}

  const Router::DirectResponseEntry* directResponseEntry() const override;
  const Router::RouteEntry* routeEntry() const override;
  const Router::Decorator* decorator() const override;
  const Router::RouteTracing* tracingConfig() const override;
  const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override;

private:
  Router::RouteConstSharedPtr base_route_;
};

} // namespace Router
} // namespace Envoy
