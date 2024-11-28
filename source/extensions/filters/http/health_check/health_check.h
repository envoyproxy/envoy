#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

/**
 * All health check filter stats. @see stats_macros.h
 */
#define ALL_HEALTH_CHECK_FILTER_STATS(COUNTER)                                                     \
  COUNTER(request_total)                                                                           \
  COUNTER(failed)                                                                                  \
  COUNTER(ok)                                                                                      \
  COUNTER(cached_response)                                                                         \
  COUNTER(failed_cluster_not_found)                                                                \
  COUNTER(failed_cluster_empty)                                                                    \
  COUNTER(failed_cluster_unhealthy)                                                                \
  COUNTER(degraded)

/**
 * Struct definition for all health check stats. @see stats_macros.h
 */
struct HealthCheckFilterStats {
  ALL_HEALTH_CHECK_FILTER_STATS(GENERATE_COUNTER_STRUCT)

  static HealthCheckFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat(prefix, "health_check.");
    return {ALL_HEALTH_CHECK_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }
};

// Forward declarations
class HealthCheckFilter;
class HealthCheckCacheManager;

using HealthCheckCacheManagerSharedPtr = std::shared_ptr<HealthCheckCacheManager>;
using HealthCheckFilterStatsSharedPtr = std::shared_ptr<HealthCheckFilterStats>;
using ClusterMinHealthyPercentages = std::map<std::string, double>;
using ClusterMinHealthyPercentagesConstSharedPtr =
    std::shared_ptr<const ClusterMinHealthyPercentages>;
using HeaderDataVectorSharedPtr = std::shared_ptr<std::vector<Http::HeaderUtility::HeaderDataPtr>>;

/**
 * Shared cache manager used by all filter instances.
 */
class HealthCheckCacheManager {
public:
  HealthCheckCacheManager(Event::Dispatcher& dispatcher, std::chrono::milliseconds timeout);

  std::pair<Http::Code, bool> getCachedResponse() {
    return {last_response_code_, last_response_degraded_};
  }
  void setCachedResponse(Http::Code code, bool degraded) {
    last_response_code_ = code;
    last_response_degraded_ = degraded;
    use_cached_response_ = true;
  }
  bool useCachedResponse() { return use_cached_response_; }

private:
  void onTimer();

  Event::TimerPtr clear_cache_timer_;
  const std::chrono::milliseconds timeout_;
  std::atomic<bool> use_cached_response_{};
  std::atomic<Http::Code> last_response_code_{};
  std::atomic<bool> last_response_degraded_{};
};

/**
 * Health check responder filter.
 */
class HealthCheckFilter : public Http::StreamFilter {
public:
  HealthCheckFilter(Server::Configuration::ServerFactoryContext& context, bool pass_through_mode,
                    HealthCheckCacheManagerSharedPtr cache_manager,
                    HeaderDataVectorSharedPtr header_match_data,
                    ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages,
                    HealthCheckFilterStatsSharedPtr stats)
      : context_(context), pass_through_mode_(pass_through_mode), cache_manager_(cache_manager),
        header_match_data_(std::move(header_match_data)),
        cluster_min_healthy_percentages_(cluster_min_healthy_percentages), stats_(stats) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
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
  void onComplete();

  Server::Configuration::ServerFactoryContext& context_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool handling_{};
  bool health_check_request_{};
  bool pass_through_mode_{};
  HealthCheckCacheManagerSharedPtr cache_manager_;
  const HeaderDataVectorSharedPtr header_match_data_;
  ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages_;
  const HealthCheckFilterStatsSharedPtr stats_;
};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
