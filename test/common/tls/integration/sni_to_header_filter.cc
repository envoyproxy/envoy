#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class SniToHeaderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "sni-to-header-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& header_map, bool) override {
    header_map.addCopy(Http::LowerCaseString("x-envoy-client-sni"),
                       decoder_callbacks_->connection()->ssl()->sni());
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char SniToHeaderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<SniToHeaderFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
