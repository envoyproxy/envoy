#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"
#include "test/integration/filters/set_route_filter_config.pb.h"
#include "test/integration/filters/set_route_filter_config.pb.validate.h"
#include "test/test_common/delegating_route_utility.h"

namespace Envoy {

class SetRouteFilterConfig {
public:
  SetRouteFilterConfig(const test::integration::filters::SetRouteFilterConfig& config)
      : proto_config_(config) {}
  const test::integration::filters::SetRouteFilterConfig proto_config_;
};

// A test filter that sets the cached route via setRoute callback and uses the
// DelegatingRoute mechanism to override the finalized upstream cluster.
class SetRouteFilter : public Http::PassThroughFilter {
public:
  SetRouteFilter(std::shared_ptr<SetRouteFilterConfig> config) : config_(config) {}

  constexpr static char name[] = "set-route-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    auto route_override = std::make_shared<Router::ExampleDerivedDelegatingRoute>(
        decoder_callbacks_->downstreamCallbacks()->route(nullptr),
        config_->proto_config_.cluster_override(),
        PROTOBUF_GET_OPTIONAL_MS(config_->proto_config_, idle_timeout_override));

    decoder_callbacks_->downstreamCallbacks()->setRoute(route_override);
    return Http::FilterHeadersStatus::Continue;
  }

private:
  const std::shared_ptr<SetRouteFilterConfig> config_;
};

class SetRouteFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                  test::integration::filters::SetRouteFilterConfig> {
public:
  SetRouteFilterFactory() : FactoryBase(SetRouteFilter::name) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::SetRouteFilterConfig& proto_config, const std::string&,
      Server::Configuration::FactoryContext&) override {
    auto filter_config = std::make_shared<SetRouteFilterConfig>(proto_config);
    return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(std::make_shared<SetRouteFilter>(filter_config));
    };
  }
};

constexpr char SetRouteFilter::name[];
REGISTER_FACTORY(SetRouteFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Envoy
