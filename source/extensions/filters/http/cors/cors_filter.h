#pragma once

#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

/**
 * All CORS filter stats. @see stats_macros.h
 */
#define ALL_CORS_STATS(COUNTER)                                                                    \
  COUNTER(origin_valid)                                                                            \
  COUNTER(origin_invalid)

/**
 * Struct definition for CORS stats. @see stats_macros.h
 */
struct CorsStats {
  ALL_CORS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the CORS filter.
 */
class CorsFilterConfig {
public:
  CorsFilterConfig(const std::string& stats_prefix, Stats::Scope& scope);
  CorsStats& stats() { return stats_; }

private:
  static CorsStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CorsStats{ALL_CORS_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CorsStats stats_;
};
using CorsFilterConfigSharedPtr = std::shared_ptr<CorsFilterConfig>;

class CorsFilter : public Http::StreamFilter {
public:
  CorsFilter(CorsFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  };

private:
  friend class CorsFilterTest;

  const std::vector<Matchers::StringMatcherPtr>* allowOrigins();
  const std::string& allowMethods();
  const std::string& allowHeaders();
  const std::string& exposeHeaders();
  const std::string& maxAge();
  bool allowCredentials();
  bool shadowEnabled();
  bool enabled();
  bool isOriginAllowed(const Http::HeaderString& origin);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  std::array<const Envoy::Router::CorsPolicy*, 2> policies_;
  bool is_cors_request_{};
  const Http::HeaderEntry* origin_{};

  CorsFilterConfigSharedPtr config_;
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
