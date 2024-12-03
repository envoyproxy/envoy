#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that stops iteration in decodeHeaders then continues in decodeBody.
class StopInHeadersContinueInBodyFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "stop-in-headers-continue-in-body-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool /* end_stream */) override {
    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
};

static Registry::RegisterFactory<SimpleFilterConfig<StopInHeadersContinueInBodyFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<StopInHeadersContinueInBodyFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;
} // namespace Envoy
