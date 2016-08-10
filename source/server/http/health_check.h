#pragma once

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

class HealthCheckFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& config, const std::string&,
                                             Server::Instance& server) override;
};

} // Configuration
} // Server

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

typedef std::shared_ptr<HealthCheckCacheManager> HealthCheckCacheManagerPtr;

/**
 * Health check responder filter.
 */
class HealthCheckFilter : public Http::StreamFilter {
public:
  HealthCheckFilter(Server::Instance& server, bool pass_through_mode,
                    HealthCheckCacheManagerPtr cache_manager, const std::string& endpoint)
      : server_(server), pass_through_mode_(pass_through_mode), cache_manager_(cache_manager),
        endpoint_(endpoint) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool bool_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

private:
  void onComplete();

  Server::Instance& server_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool handling_{};
  bool health_check_request_{};
  bool pass_through_mode_{};
  HealthCheckCacheManagerPtr cache_manager_{};
  const std::string endpoint_;
};
