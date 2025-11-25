#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

class EncodeHeadersReturnStopIterationFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encode-headers-return-stop-iteration-filter";
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    if (end_stream) {
      return Envoy::Http::FilterHeadersStatus::Continue;
    } else {
      return Envoy::Http::FilterHeadersStatus::StopIteration;
    }
  }
};

static Registry::RegisterFactory<SimpleFilterConfig<EncodeHeadersReturnStopIterationFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<EncodeHeadersReturnStopIterationFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
