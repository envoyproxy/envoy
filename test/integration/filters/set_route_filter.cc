#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"
#include "test/test_common/delegating_route_utility.h"

namespace Envoy {

// A test filter that sets the cached route via setRoute callback and uses the
// DelegatingRoute mechanism to override the finalized upstream cluster.
class SetRouteFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "set-route-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    auto route_override = std::make_shared<Router::ExampleDerivedDelegatingRoute>(
        decoder_callbacks_->route(), "cluster_override");

    decoder_callbacks_->setRoute(route_override);
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char SetRouteFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<SetRouteFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
