#pragma once

#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/router/shadow_writer.h"
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
  struct TimeoutData {
    std::chrono::milliseconds global_timeout_{0};
    std::chrono::milliseconds per_try_timeout_{0};
  };

  /**
   * Determine whether a request should be shadowed.
   * @param policy supplies the route's shadow policy.
   * @param runtime supplies the runtime to lookup the shadow key in.
   * @param stable_random supplies the random number to use when determining whether shadowing
   *        should take place.
   * @return TRUE if shadowing should take place.
   */
  static bool shouldShadow(const ShadowPolicy& policy, Runtime::Loader& runtime,
                           uint64_t stable_random);

  /**
   * Determine the final timeout to use based on the route as well as the request headers.
   * @param route supplies the request route.
   * @param request_headers supplies the request headers.
   * @return TimeoutData for both the global and per try timeouts.
   */
  static TimeoutData finalTimeout(const RouteEntry& route, Http::HeaderMap& request_headers);
};

/**
 * Configuration for the router filter.
 */
class FilterConfig {
public:
  FilterConfig(const std::string& stat_prefix, const std::string& service_zone, Stats::Store& stats,
               Upstream::ClusterManager& cm, Runtime::Loader& runtime,
               Runtime::RandomGenerator& random, ShadowWriterPtr&& shadow_writer,
               bool emit_dynamic_stats)
      : stats_store_(stats), service_zone_(service_zone), cm_(cm), runtime_(runtime),
        random_(random), stats_{ALL_ROUTER_STATS(POOL_COUNTER_PREFIX(stats, stat_prefix))},
        emit_dynamic_stats_(emit_dynamic_stats), shadow_writer_(std::move(shadow_writer)) {}

  ShadowWriter& shadowWriter() { return *shadow_writer_; }

  Stats::Store& stats_store_;
  const std::string service_zone_;
  Upstream::ClusterManager& cm_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  FilterStats stats_;
  const bool emit_dynamic_stats_;

private:
  ShadowWriterPtr shadow_writer_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigPtr;

/**
 * Service routing filter.
 */
class Filter : Logger::Loggable<Logger::Id::router>, public Http::StreamDecoderFilter {
public:
  Filter(FilterConfig& config);
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
    ~UpstreamRequest();

    void setupPerTryTimeout();
    void onPerTryTimeout();

    // Http::StreamDecoder
    void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override;

    // Http::PooledStreamEncoderCallbacks
    void onUpstreamHostSelected(Upstream::HostDescriptionPtr host) override {
      parent_.upstream_host_ = host;
      parent_.callbacks_->requestInfo().onUpstreamHostSelected(host);
    }

    Filter& parent_;
    Http::PooledStreamEncoderPtr upstream_encoder_;
    bool upstream_canary_{};
    Event::TimerPtr per_try_timeout_;
  };

  typedef std::unique_ptr<UpstreamRequest> UpstreamRequestPtr;

  enum class UpstreamResetType { Reset, GlobalTimeout, PerTryTimeout };

  Http::AccessLog::FailureReason
  streamResetReasonToFailureReason(Http::StreamResetReason reset_reason);

  const std::string& upstreamZone();
  void chargeUpstreamCode(const Http::HeaderMap& response_headers);
  void chargeUpstreamCode(Http::Code code);
  void cleanup();
  virtual RetryStatePtr createRetryState(const RetryPolicy& policy,
                                         Http::HeaderMap& request_headers,
                                         const Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                         Runtime::RandomGenerator& random,
                                         Event::Dispatcher& dispatcher,
                                         Upstream::ResourcePriority priority) PURE;
  Upstream::ResourcePriority finalPriority();
  void maybeDoShadowing();
  void onRequestComplete();
  void onResetStream();
  void onResponseTimeout();
  void onUpstreamHeaders(Http::HeaderMapPtr&& headers, bool end_stream);
  void onUpstreamData(Buffer::Instance& data, bool end_stream);
  void onUpstreamTrailers(Http::HeaderMapPtr&& trailers);
  void onUpstreamComplete();
  void onUpstreamReset(UpstreamResetType type,
                       const Optional<Http::StreamResetReason>& reset_reason);
  void sendNoHealthyUpstreamResponse();
  bool setupRetry(bool end_stream);
  void doRetry();

  FilterConfig& config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  const RouteEntry* route_;
  const Upstream::Cluster* cluster_;
  std::list<std::string> alt_stat_prefixes_;
  const VirtualCluster* request_vcluster_;
  bool downstream_response_started_{};
  Event::TimerPtr response_timeout_;
  FilterUtility::TimeoutData timeout_;
  UpstreamRequestPtr upstream_request_;
  RetryStatePtr retry_state_;
  Http::HeaderMap* downstream_headers_{};
  Http::HeaderMap* downstream_trailers_{};
  bool downstream_end_stream_{};
  bool do_shadowing_{};
  Upstream::HostDescriptionPtr upstream_host_;
};

class ProdFilter : public Filter {
public:
  using Filter::Filter;

private:
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                                 const Upstream::Cluster& cluster, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                                 Upstream::ResourcePriority priority) override;
};

} // Router
