#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// Registers the misbehaving filter which removes all response headers.
class RemoveResponseHeadersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "remove-response-headers-filter";
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    std::vector<std::string> keys;
    headers.iterate([&keys](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      keys.push_back(std::string(header.key().getStringView()));
      return Http::HeaderMap::Iterate::Continue;
    });
    for (auto& k : keys) {
      const Http::LowerCaseString lower_key{k};
      headers.remove(lower_key);
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

static Registry::RegisterFactory<SimpleFilterConfig<RemoveResponseHeadersFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<RemoveResponseHeadersFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
