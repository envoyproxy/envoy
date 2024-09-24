#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// Registers a filter which removes all trailers, leaving an empty trailers block.
// This resembles the behavior of grpc_web_filter, so we can verify that the codecs
// do the right thing when that happens.
class RemoveResponseTrailersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "remove-response-trailers-filter";
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    std::vector<std::string> keys;
    trailers.iterate([&keys](const Http::HeaderEntry& trailer) -> Http::HeaderMap::Iterate {
      keys.push_back(std::string(trailer.key().getStringView()));
      return Http::HeaderMap::Iterate::Continue;
    });
    for (auto& k : keys) {
      const Http::LowerCaseString lower_key{k};
      trailers.remove(lower_key);
    }
    return Http::FilterTrailersStatus::Continue;
  }
};

static Registry::RegisterFactory<SimpleFilterConfig<RemoveResponseTrailersFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<RemoveResponseTrailersFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
