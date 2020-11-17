#pragma once

#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

/**
 * A filter that will crash Envoy if IsKillRequestEnabled() returns true and
 * incoming request contains HTTP KillRequest header with values in
 * one of (case-insensitive) ["true", "t", "yes", "y", "1"].
 */
class KillRequestFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  KillRequestFilter(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& kill_request,
      Random::RandomGenerator& random_generator)
      : kill_request_(kill_request), random_generator_(random_generator) {}

  ~KillRequestFilter() override {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {}

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {}

private:
  // Return a random boolean value, with probability configured in KillRequest
  // equaling true.
  bool IsKillRequestEnabled();

  const envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request_;
  Random::RandomGenerator& random_generator_;
};

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
