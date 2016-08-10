#pragma once

#include "envoy/http/filter.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

namespace Http {

/**
 * Global configuration for the HTTP rate limit filter.
 */
class RateLimitFilterConfig {
public:
  RateLimitFilterConfig(const Json::Object& config, const std::string& local_service_cluster,
                        Stats::Store& stats_store, Runtime::Loader& runtime);

  const std::string& domain() { return domain_; }
  const std::string& localServiceCluster() { return local_service_cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Store& stats() { return stats_store_; }

private:
  const std::string domain_;
  const std::string local_service_cluster_;
  Stats::Store& stats_store_;
  Runtime::Loader& runtime_;
};

typedef std::shared_ptr<RateLimitFilterConfig> RateLimitFilterConfigPtr;

/**
 * HTTP rate limit filter. Depending on the route configuration, this filter calls the global
 * rate limiting service before allowing further filter iteration.
 */
class RateLimitFilter : public StreamDecoderFilter, public RateLimit::RequestCallbacks {
public:
  RateLimitFilter(RateLimitFilterConfigPtr config, RateLimit::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // RateLimit::RequestCallbacks
  void complete(RateLimit::LimitStatus status) override;

private:
  enum class State { NotStarted, Calling, Complete, Responded };

  static const Http::HeaderMapImpl TOO_MANY_REQUESTS_HEADER;

  RateLimitFilterConfigPtr config_;
  RateLimit::ClientPtr client_;
  StreamDecoderFilterCallbacks* callbacks_{};
  bool initiating_call_{};
  State state_{State::NotStarted};
  std::string cluster_ratelimit_stat_prefix_;
  std::string cluster_stat_prefix_;
};

} // Http
