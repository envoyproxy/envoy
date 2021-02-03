#pragma once

#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/http/header_utility.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

class KillRequestHeaderNameValues {
public:
  const char* prefix() const { return ThreadSafeSingleton<Http::PrefixValue>::get().prefix(); }

  const Http::LowerCaseString KillRequest{absl::StrCat(prefix(), "-kill-request")};
};

using KillRequestHeaders = ConstSingleton<KillRequestHeaderNameValues>;

/**
 * A filter that will crash Envoy if IsKillRequestEnabled() return true and
 * incoming request contains HTTP KillRequest header with values in
 * one of (case-insensitive) ["true", "t", "yes", "y", "1"].
 */
class KillRequestFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  KillRequestFilter(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest& kill_request,
      Random::RandomGenerator& random_generator)
      : kill_request_(kill_request), random_generator_(random_generator) {}

  ~KillRequestFilter() override = default;

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

  void setDecoderFilterCallbacks(
      Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

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
  // Return a random boolean value, with probability configured in KillRequest
  // equaling true.
  bool isKillRequestEnabled();

  envoy::extensions::filters::http::kill_request::v3::KillRequest kill_request_;
  Random::RandomGenerator& random_generator_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

/**
 * Configuration for fault injection.
 */
class KillSettings : public Router::RouteSpecificFilterConfig {
public:
  KillSettings(
      const envoy::extensions::filters::http::kill_request::v3::KillRequest&
          kill_request);

  const std::vector<Http::HeaderUtility::HeaderDataPtr>& filterHeaders() const {
    return kill_request_filter_headers_;
  }

  const envoy::type::v3::FractionalPercent getProbability() const {
    return std::move(kill_probability_);
  }

private:
  envoy::type::v3::FractionalPercent kill_probability_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr>
      kill_request_filter_headers_;
};

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
