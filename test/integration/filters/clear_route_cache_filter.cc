#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that clears the route cache on creation
class ClearRouteCacheFilter : public Http::PassThroughFilter {
public:
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks.downstreamCallbacks()->clearRouteCache();
    Http::PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  }
};

class ClearRouteCacheFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  ClearRouteCacheFilterConfig() : EmptyHttpFilterConfig("clear-route-cache") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ClearRouteCacheFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<ClearRouteCacheFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
