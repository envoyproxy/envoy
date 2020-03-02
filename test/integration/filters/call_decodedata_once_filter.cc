#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter that only allows decodeData() to be called once with fixed data length.
class CallDecodeDataOnceFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "call-decodedata-once-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& header_map, bool) override {
    const Http::HeaderEntry* entry_content =
        header_map.get(Envoy::Http::LowerCaseString("content_size"));
    const Http::HeaderEntry* entry_added =
        header_map.get(Envoy::Http::LowerCaseString("added_size"));
    ASSERT(entry_content != nullptr && entry_added != nullptr);
    content_size_ = std::stoul(std::string(entry_content->value().getStringView()));
    added_size_ = std::stoul(std::string(entry_added->value().getStringView()));
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    // Request data length (size 5000) + data from addDecodedData() called in dataDecode (size 1).
    // Or data from addDecodedData() called in dataTrailers (size 1)
    EXPECT_TRUE(data.length() == content_size_ + added_size_ || data.length() == added_size_);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

private:
  size_t content_size_ = 0;
  size_t added_size_ = 0;
};

constexpr char CallDecodeDataOnceFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<CallDecodeDataOnceFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
