#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "common/router/config_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

/**
 * Shared cache manager used by all instances of a health check filter configuration as well as
 * all threads. This sets up a timer that will invalidate the cached response code and allow some
 * requests to go through to the backend. No attempt is made to allow only a single request to go
 * through to the backend, so during the invalidation window some number of requests will get
 * through.
 */
class HealthCheckCacheManager {
public:
  HealthCheckCacheManager(Event::Dispatcher& dispatcher, std::chrono::milliseconds timeout);

  Http::Code getCachedResponseCode() { return last_response_code_; }
  void setCachedResponseCode(Http::Code code) {
    last_response_code_ = code;
    use_cached_response_code_ = true;
  }
  bool useCachedResponseCode() { return use_cached_response_code_; }

private:
  void onTimer();

  Event::TimerPtr clear_cache_timer_;
  const std::chrono::milliseconds timeout_;
  std::atomic<bool> use_cached_response_code_{};
  std::atomic<Http::Code> last_response_code_{};
};

typedef std::shared_ptr<HealthCheckCacheManager> HealthCheckCacheManagerSharedPtr;

typedef std::map<std::string, double> ClusterMinHealthyPercentages;
typedef std::shared_ptr<const ClusterMinHealthyPercentages>
    ClusterMinHealthyPercentagesConstSharedPtr;

typedef std::shared_ptr<std::vector<Router::ConfigUtility::HeaderData>> HeaderDataVectorSharedPtr;

/**
 * Health check responder filter.
 */
class HealthCheckFilter : public Http::StreamFilter {
public:
  HealthCheckFilter(Server::Configuration::FactoryContext& context, bool pass_through_mode,
                    HealthCheckCacheManagerSharedPtr cache_manager,
                    HeaderDataVectorSharedPtr header_match_data,
                    ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages)
      : context_(context), pass_through_mode_(pass_through_mode), cache_manager_(cache_manager),
        header_match_data_(std::move(header_match_data)),
        cluster_min_healthy_percentages_(cluster_min_healthy_percentages) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

private:
  void onComplete();

  Server::Configuration::FactoryContext& context_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool handling_{};
  bool health_check_request_{};
  bool pass_through_mode_{};
  HealthCheckCacheManagerSharedPtr cache_manager_;
  const HeaderDataVectorSharedPtr header_match_data_;
  ClusterMinHealthyPercentagesConstSharedPtr cluster_min_healthy_percentages_;
};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
