#pragma once

#include "envoy/http/filter.h"

#include "source/extensions/common/matcher/matcher.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "mobile/library/common/extensions/filters/http/assertion/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {

class AssertionFilterConfig {
public:
  AssertionFilterConfig(
      const envoymobile::extensions::filters::http::assertion::Assertion& proto_config);

  Extensions::Common::Matcher::Matcher& rootMatcher() const;
  size_t matchersSize() const { return matchers_.size(); }

private:
  std::vector<Extensions::Common::Matcher::MatcherPtr> matchers_;
};

using AssertionFilterConfigSharedPtr = std::shared_ptr<AssertionFilterConfig>;

/**
 * Filter to assert expectations on HTTP requests.
 */
class AssertionFilter final : public Http::PassThroughFilter {
public:
  AssertionFilter(AssertionFilterConfigSharedPtr config);

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  const AssertionFilterConfigSharedPtr config_;
  Extensions::Common::Matcher::Matcher::MatchStatusVector statuses_;
};

} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
