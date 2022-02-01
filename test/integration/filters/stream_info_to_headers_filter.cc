#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

uint64_t toMs(MonotonicTime time) {
  return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

} // namespace

// A filter that sticks stream info into headers for integration testing.
class StreamInfoToHeadersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "stream-info-to-headers-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    const std::string dns_start = "envoy.dynamic_forward_proxy.dns_start_ms";
    const std::string dns_end = "envoy.dynamic_forward_proxy.dns_end_ms";
    StreamInfo::StreamInfo& stream_info = decoder_callbacks_->streamInfo();

    if (stream_info.downstreamTiming().getValue(dns_start).has_value()) {
      headers.addCopy(
          Http::LowerCaseString("dns_start"),
          absl::StrCat(toMs(stream_info.downstreamTiming().getValue(dns_start).value())));
    }
    if (stream_info.downstreamTiming().getValue(dns_end).has_value()) {
      headers.addCopy(Http::LowerCaseString("dns_end"),
                      absl::StrCat(toMs(stream_info.downstreamTiming().getValue(dns_end).value())));
    }
    if (decoder_callbacks_->streamInfo().upstreamInfo()) {
      if (decoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection()) {
        headers.addCopy(
            Http::LowerCaseString("alpn"),
            decoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection()->alpn());
      }
      headers.addCopy(Http::LowerCaseString("num_streams"),
                      decoder_callbacks_->streamInfo().upstreamInfo()->upstreamNumStreams());
    }

    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    ASSERT(decoder_callbacks_->streamInfo().upstreamInfo());
    StreamInfo::UpstreamTiming& upstream_timing =
        decoder_callbacks_->streamInfo().upstreamInfo()->upstreamTiming();
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
