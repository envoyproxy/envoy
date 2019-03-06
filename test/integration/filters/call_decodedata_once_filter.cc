#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter that only allows decodeData() to be called once with fixed data length.
class CallDecodeDataOnceFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "call-decodedata-once-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    // Request data length (size 5000) + data from addDecodedData() called in dataDecode (size 1).
    // Or data from addDecodedData() called in dataTrailers (size 1)
    EXPECT_TRUE(data.length() == 70001 || data.length() == 1);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
};

constexpr char CallDecodeDataOnceFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<CallDecodeDataOnceFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
