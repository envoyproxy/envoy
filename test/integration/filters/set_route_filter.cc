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
    std::shared_ptr<const ExampleDelegatingRouteDerived> route_override =
        std::make_shared<ExampleDelegatingRouteDerived>(decoder_callbacks_->route(),
                                                        "cluster-override");

    decoder_callbacks_->setRoute(route_override);
    return Http::FilterHeadersStatus::Continue;
  }

  // Unlikely to be reused by other int tests, scoping to this test filter.
  // TODO: maybe put in shared place for test/common/http/conn_manager_impl_test_base.h to also
  // access
  class ExampleDelegatingRouteEntryDerived : public Router::DelegatingRouteEntry {
  public:
    ExampleDelegatingRouteEntryDerived(const Router::RouteEntry* base_route_entry,
                                       const std::string& cluster_name_override)
        : DelegatingRouteEntry(base_route_entry), custom_cluster_name_(cluster_name_override) {}

    const std::string& clusterName() const override { return custom_cluster_name_; }

  private:
    const std::string& custom_cluster_name_;
  };

  class ExampleDelegatingRouteDerived : public Router::DelegatingRoute {
  public:
    ExampleDelegatingRouteDerived(Router::RouteConstSharedPtr base_route,
                                  const std::string& cluster_name_override)
        : DelegatingRoute(base_route), custom_route_entry_(new ExampleDelegatingRouteEntryDerived(
                                           base_route->routeEntry(), cluster_name_override)) {}

    const Router::RouteEntry* routeEntry() const override { return custom_route_entry_; }

  private:
    ExampleDelegatingRouteEntryDerived* custom_route_entry_;
  };
};

constexpr char SetRouteFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<SetRouteFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
