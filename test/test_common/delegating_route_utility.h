#include <chrono>

#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Router {

// For testing purposes only. Used in both HCM unit tests and integration_test.

// Example derived class of DelegatingRouteEntry.
class ExampleDerivedDelegatingRouteEntry : public Router::DelegatingRouteEntry {
public:
  ExampleDerivedDelegatingRouteEntry(
      Router::RouteConstSharedPtr base_route, const std::string& cluster_name_override,
      absl::optional<std::chrono::milliseconds> idle_timeout_override = absl::nullopt)
      : DelegatingRouteEntry(std::move(base_route)), custom_cluster_name_(cluster_name_override),
        custom_idle_timeout_(idle_timeout_override) {}

  const std::string& clusterName() const override { return custom_cluster_name_; }
  const RouteStatsContextOptRef routeStatsContext() const override {
    return RouteStatsContextOptRef();
  }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override {
    return custom_idle_timeout_.has_value() ? custom_idle_timeout_
                                            : Router::DelegatingRouteEntry::idleTimeout();
  }

private:
  const std::string custom_cluster_name_;
  const absl::optional<std::chrono::milliseconds> custom_idle_timeout_;
};

// Example derived class of DelegatingRoute. Leverages ExampleDerivedDelegatingRouteEntry to create
// a route with a custom upstream cluster override.
class ExampleDerivedDelegatingRoute : public Router::DelegatingRoute {
public:
  ExampleDerivedDelegatingRoute(
      Router::RouteConstSharedPtr base_route, const std::string& cluster_name_override,
      absl::optional<std::chrono::milliseconds> idle_timeout_override = absl::nullopt)
      : DelegatingRoute(base_route),
        custom_route_entry_(std::make_unique<const ExampleDerivedDelegatingRouteEntry>(
            std::move(base_route), cluster_name_override, idle_timeout_override)) {}

  const Router::RouteEntry* routeEntry() const override { return custom_route_entry_.get(); }

private:
  const std::unique_ptr<const ExampleDerivedDelegatingRouteEntry> custom_route_entry_;
};

} // namespace Router
} // namespace Envoy
