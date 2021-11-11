#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter that sticks stream info into headers for integration testing.
class StreamInfoToHeadersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "stream-info-to-headers-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    if (decoder_callbacks_->streamInfo().upstreamSslConnection()) {
      headers.addCopy(Http::LowerCaseString("alpn"),
                      decoder_callbacks_->streamInfo().upstreamSslConnection()->alpn());
    }

    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    StreamInfo::UpstreamTiming& upstream_timing = decoder_callbacks_->streamInfo().upstreamTiming();
    // Upstream metrics aren't available until the response is complete.
    if (upstream_timing.upstream_connect_start_.has_value()) {
      trailers.addCopy(
          Http::LowerCaseString("upstream_connect_start"),
          absl::StrCat(upstream_timing.upstream_connect_start_.value().time_since_epoch().count()));
    }
    if (upstream_timing.upstream_connect_complete_.has_value()) {
      trailers.addCopy(
          Http::LowerCaseString("upstream_connect_complete"),
          absl::StrCat(
              upstream_timing.upstream_connect_complete_.value().time_since_epoch().count()));
    }
    if (upstream_timing.upstream_handshake_complete_.has_value()) {
      trailers.addCopy(
          Http::LowerCaseString("upstream_handshake_complete"),
          absl::StrCat(
              upstream_timing.upstream_handshake_complete_.value().time_since_epoch().count()));
    }
    return Http::FilterTrailersStatus::Continue;
  }
};

constexpr char StreamInfoToHeadersFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<StreamInfoToHeadersFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
