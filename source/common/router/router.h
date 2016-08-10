#pragma once

#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/http/pooled_stream_encoder.h"

namespace Router {

/**
 * All router filter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_ROUTER_STATS(COUNTER)                                                                  \
  COUNTER(no_route)                                                                                \
  COUNTER(rq_redirect)                                                                             \
  COUNTER(rq_total)
// clang-format on

/**
 * Struct definition for all router filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_ROUTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Router filter utilities split out for ease of testing.
 */
class FilterUtility {
public:
  /**
   * Determine the final timeout to use based on the route as well as the request headers.
   * @param route supplies the request route.
   * @param request_headers supplies the request headers.
   */
  static std::chrono::milliseconds finalTimeout(const RouteEntry& route,
                                                Http::HeaderMap& request_headers);
};

/**
 * Service routing filter.
 */
class Filter : Logger::Loggable<Logger::Id::router>, public Http::StreamDecoderFilter {
public:
  Filter(const std::string& stat_prefix, Stats::Store& stats, Upstream::ClusterManager& cm,
         Runtime::Loader& runtime, Runtime::RandomGenerator& random);
  ~Filter();

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks_->addResetStreamCallback([this]() -> void { onResetStream(); });
  }

private:
  struct UpstreamRequest : public Http::StreamDecoder,
                           public Http::StreamCallbacks,
                           public Http::PooledStreamEncoderCallbacks {
    UpstreamRequest(Filter& parent, Http::ConnectionPool::Instance& pool);

    // Http::StreamDecoder
    void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(const Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override;

    // Http::PooledStreamEncoderCallbacks
    void onUpstreamHostSelected(Upstream::HostDescriptionPtr host) override {
      parent_.callbacks_->requestInfo().onUpstreamHostSelected(host);
    }

    Filter& parent_;
    Http::PooledStreamEncoderPtr upstream_encoder_;
    bool upstream_canary_{};
  };

  typedef std::unique_ptr<UpstreamRequest> UpstreamRequestPtr;

  Http::AccessLog::FailureReason
  streamResetReasonToFailureReason(Http::StreamResetReason reset_reason);

  void chargeUpstreamCode(const Http::HeaderMap& response_headers);
  void chargeUpstreamCode(Http::Code code);
  void cleanup();
  virtual RetryStatePtr createRetryState(const RetryPolicy& policy,
                                         Http::HeaderMap& request_headers,
                                         const Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                         Runtime::RandomGenerator& random,
                                         Event::Dispatcher& dispatcher) PURE;
  void onRequestComplete();
  void onResetStream();
  void onResponseTimeout();
  void onUpstreamHeaders(Http::HeaderMapPtr&& headers, bool end_stream);
  void onUpstreamData(const Buffer::Instance& data, bool end_stream);
  void onUpstreamTrailers(Http::HeaderMapPtr&& trailers);
  void onUpstreamComplete();
  void onUpstreamReset(bool timeout, const Optional<Http::StreamResetReason>& reset_reason);
  void sendNoHealthyUpstreamResponse();
  bool setupRetry(bool end_stream);
  void doRetry();

  Stats::Store& stats_store_;
  Upstream::ClusterManager& cm_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  const RouteEntry* route_;
  std::string stat_prefix_;
  std::list<std::string> alt_stat_prefixes_;
  std::string request_vcluster_name_;
  bool downstream_response_started_{};
  Event::TimerPtr response_timeout_;
  FilterStats stats_;
  std::chrono::milliseconds timeout_;
  UpstreamRequestPtr upstream_request_;
  RetryStatePtr retry_state_;
  Http::HeaderMap* downstream_headers_{};
  bool downstream_end_stream_{};
};

class ProdFilter : public Filter {
public:
  using Filter::Filter;

private:
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                                 const Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random,
                                 Event::Dispatcher& dispatcher) override;
};

} // Router
