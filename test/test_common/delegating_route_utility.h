#include "common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

// For testing purposes only. Used in both HCM unit tests and integration_test.

// Example derived class of DelegatingRouteEntry.
class ExampleDerivedDelegatingRouteEntry : public Router::DelegatingRouteEntry {
public:
  ExampleDerivedDelegatingRouteEntry(Router::RouteConstSharedPtr base_route,
                                     const std::string& cluster_name_override)
      : DelegatingRouteEntry(std::move(base_route)), custom_cluster_name_(cluster_name_override) {}

  const std::string& clusterName() const override { return custom_cluster_name_; }

private:
  const std::string custom_cluster_name_;
};

// Example derived class of DelegatingRoute. Leverages ExampleDerivedDelegatingRouteEntry to create
// a route with a custom upstream cluster override.
class ExampleDerivedDelegatingRoute : public Router::DelegatingRoute {
public:
  ExampleDerivedDelegatingRoute(Router::RouteConstSharedPtr base_route,
                                const std::string& cluster_name_override)
      : DelegatingRoute(base_route),
        custom_route_entry_(std::make_unique<const ExampleDerivedDelegatingRouteEntry>(
            std::move(base_route), cluster_name_override)) {}

  const Router::RouteEntry* routeEntry() const override { return custom_route_entry_.get(); }

private:
  const std::unique_ptr<const ExampleDerivedDelegatingRouteEntry> custom_route_entry_;
};

} // namespace Router
} // namespace Envoy
