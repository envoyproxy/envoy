#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

// A filter that only allows decodeData() to be called once. Multiple calls will result in assert
// failure.
class CallDecodeDataOnceFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "call-decodedata-once-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ASSERT(call_count_++ == 0);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

private:
  int call_count_ = 0;
};

constexpr char CallDecodeDataOnceFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<CallDecodeDataOnceFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
