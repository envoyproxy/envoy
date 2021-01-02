#pragma once

#include <string>

#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TcpPost {

/**
 * A filter that will crash Envoy if IsTcpPostEnabled() return true and
 * incoming request contains HTTP TcpPost header with values in
 * one of (case-insensitive) ["true", "t", "yes", "y", "1"].
 */
class TcpPostFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  TcpPostFilter(const envoy::extensions::filters::http::tcp_post::v3::TcpPost& config)
      : config_(config) {}

  ~TcpPostFilter() override = default;

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override {}

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

private:
  const envoy::extensions::filters::http::tcp_post::v3::TcpPost config_;
};

} // namespace TcpPost
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
