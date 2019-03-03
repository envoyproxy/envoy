#pragma once

#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

/**
 * All CSRF filter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CSRF_STATS(COUNTER) \
  COUNTER(missing_source_origin)\
  COUNTER(request_invalid)      \
  COUNTER(request_valid)        \
// clang-format on

/**
 * Struct definition for CSRF stats. @see stats_macros.h
 */
struct CsrfStats {
  ALL_CSRF_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the CSRF filter.
 */
class CsrfFilterConfig {
public:
  CsrfFilterConfig(const std::string& stats_prefix, Stats::Scope& scope);
  CsrfStats& stats() { return stats_; }

private:
  static CsrfStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CsrfStats{ALL_CSRF_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CsrfStats stats_;
};
typedef std::shared_ptr<CsrfFilterConfig> CsrfFilterConfigSharedPtr;

class CsrfFilter : public Http::StreamDecoderFilter {
public:
  CsrfFilter(CsrfFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

private:
  friend class CsrfFilterTest;

  bool modifyMethod(const Http::HeaderMap& headers);
  absl::string_view sourceOriginValue(const Http::HeaderMap& headers);
  absl::string_view targetOriginValue(const Http::HeaderMap& headers);
  absl::string_view hostAndPort(const Envoy::Http::HeaderEntry* header);
  bool shadowEnabled();
  bool enabled();

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  std::array<const Envoy::Router::CsrfPolicy*, 2> policies_;
  CsrfFilterConfigSharedPtr config_;
};

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
