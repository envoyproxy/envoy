#include "test/integration/filters/repick_cluster_filter.h"

#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace RepickClusterFilter {

// A test filter that modifies the request header (i.e. map the cluster header
// to cluster name), clear the route cache.
class RepickClusterFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_header, bool) override {
    request_header.addCopy(Envoy::Http::LowerCaseString(ClusterHeaderName), ClusterName);
    decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    return Http::FilterHeadersStatus::Continue;
  }
};

class RepickClusterFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  RepickClusterFilterConfig() : EmptyHttpFilterConfig("repick-cluster-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(
          std::make_shared<::Envoy::RepickClusterFilter::RepickClusterFilter>());
    };
  }
};

// Perform static registration
static Registry::RegisterFactory<RepickClusterFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace RepickClusterFilter
} // namespace Envoy
