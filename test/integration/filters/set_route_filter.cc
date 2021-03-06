#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/router/delegating_route_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that sets the cached route via setRoute callback and uses the
// DelegatingRoute mechanism to override the finalized upstream cluster.
class SetRouteFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "set-route-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    std::shared_ptr<const ExampleDerivedDelegatingRoute> route_override =
        std::make_shared<ExampleDerivedDelegatingRoute>(decoder_callbacks_->route(),
                                                        "cluster_override");

    decoder_callbacks_->setRoute(route_override);
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO: Put in one place to share with unit tests (test/common/http:conn_manager_impl_test)
  // For testing purposes only. Example derived class of DelegatingRouteEntry.
  class ExampleDerivedDelegatingRouteEntry : public Router::DelegatingRouteEntry {
  public:
    ExampleDerivedDelegatingRouteEntry(Router::RouteConstSharedPtr base_route,
                                       const std::string& cluster_name_override)
        : DelegatingRouteEntry(base_route), custom_cluster_name_(cluster_name_override) {}

    const std::string& clusterName() const override { return custom_cluster_name_; }

  private:
    const std::string custom_cluster_name_;
  };

  // For testing purposes only. Example derived class of DelegatingRoute.
  class ExampleDerivedDelegatingRoute : public Router::DelegatingRoute {
  public:
    ExampleDerivedDelegatingRoute(Router::RouteConstSharedPtr base_route,
                                  const std::string& cluster_name_override)
        : DelegatingRoute(base_route),
          custom_route_entry_(std::make_unique<const ExampleDerivedDelegatingRouteEntry>(
              base_route, cluster_name_override)) {}

    const Router::RouteEntry* routeEntry() const override { return custom_route_entry_.get(); }

  private:
    const std::unique_ptr<const ExampleDerivedDelegatingRouteEntry> custom_route_entry_;
  };
};

constexpr char SetRouteFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<SetRouteFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
