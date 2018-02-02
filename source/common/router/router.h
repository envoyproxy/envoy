#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/api/v2/filter/http/router.pb.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/access_log/access_log_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/logger.h"
#include "common/http/utility.h"
#include "common/request_info/request_info_impl.h"

namespace Envoy {
namespace Router {

/**
 * All router filter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_ROUTER_STATS(COUNTER)                                                                  \
  COUNTER(no_route)                                                                                \
  COUNTER(no_cluster)                                                                              \
  COUNTER(rq_redirect)                                                                             \
  COUNTER(rq_direct_response)                                                                      \
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
   * Set the :scheme header based on the properties of the upstream cluster.
   */
  static void setUpstreamScheme(Http::HeaderMap& headers, const Upstream::ClusterInfo& cluster);

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
  FilterConfig(const std::string& stat_prefix, const LocalInfo::LocalInfo& local_info,
               Stats::Scope& scope, Upstream::ClusterManager& cm, Runtime::Loader& runtime,
               Runtime::RandomGenerator& random, ShadowWriterPtr&& shadow_writer,
               bool emit_dynamic_stats, bool start_child_span)
      : scope_(scope), local_info_(local_info), cm_(cm), runtime_(runtime),
        random_(random), stats_{ALL_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, stat_prefix))},
        emit_dynamic_stats_(emit_dynamic_stats), start_child_span_(start_child_span),
        shadow_writer_(std::move(shadow_writer)) {}

  FilterConfig(const std::string& stat_prefix, Server::Configuration::FactoryContext& context,
               ShadowWriterPtr&& shadow_writer, const envoy::api::v2::filter::http::Router& config)
      : FilterConfig(stat_prefix, context.localInfo(), context.scope(), context.clusterManager(),
                     context.runtime(), context.random(), std::move(shadow_writer),
                     PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, dynamic_stats, true),
                     config.start_child_span()) {
    for (const auto& upstream_log : config.upstream_log()) {
      upstream_logs_.push_back(AccessLog::AccessLogFactory::fromProto(upstream_log, context));
    }
  }

  ShadowWriter& shadowWriter() { return *shadow_writer_; }

  Stats::Scope& scope_;
  const LocalInfo::LocalInfo& local_info_;
  Upstream::ClusterManager& cm_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;
  FilterStats stats_;
  const bool emit_dynamic_stats_;
  const bool start_child_span_;
  std::list<AccessLog::InstanceSharedPtr> upstream_logs_;

private:
  ShadowWriterPtr shadow_writer_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

/**
 * Service routing filter.
 */
class Filter : Logger::Loggable<Logger::Id::router>,
               public Http::StreamDecoderFilter,
               public Upstream::LoadBalancerContext {
public:
  Filter(FilterConfig& config)
      : config_(config), downstream_response_started_(false), downstream_end_stream_(false),
        do_shadowing_(false) {}

  ~Filter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Upstream::LoadBalancerContext
  Optional<uint64_t> computeHashKey() override {
    if (route_entry_ && downstream_headers_) {
      auto hash_policy = route_entry_->hashPolicy();
      if (hash_policy) {
        return hash_policy->generateHash(
            callbacks_->requestInfo().downstreamRemoteAddress()->asString(), *downstream_headers_,
            [this](const std::string& key, std::chrono::seconds max_age) {
              return addDownstreamSetCookie(key, max_age);
            });
      }
    }
    return {};
  }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    if (route_entry_) {
      return route_entry_->metadataMatchCriteria();
    }
    return nullptr;
  }
  const Network::Connection* downstreamConnection() const override {
    return callbacks_->connection();
  }

  /**
   * Set a computed cookie to be sent with the downstream headers.
   * @param key supplies the size of the cookie
   * @param max_age the lifetime of the cookie
   * @return std::string the value of the new cookie
   */
  std::string addDownstreamSetCookie(const std::string& key, std::chrono::seconds max_age) {
    // The cookie value should be the same per connection so that if multiple
    // streams race on the same path, they all receive the same cookie.
    // Since the downstream port is part of the hashed value, multiple HTTP1
    // connections can receive different cookies if they race on requests.
    std::string value;
    const Network::Connection* conn = downstreamConnection();
    // Need to check for null conn if this is ever used by Http::AsyncClient in the future.
    value = conn->remoteAddress()->asString() + conn->localAddress()->asString();

    const std::string cookie_value = Hex::uint64ToHex(HashUtil::xxHash64(value));
    downstream_set_cookies_.emplace_back(
        Http::Utility::makeSetCookieValue(key, cookie_value, max_age));
    return cookie_value;
  }

protected:
  RetryStatePtr retry_state_;

private:
  struct UpstreamRequest : public Http::StreamDecoder,
                           public Http::StreamCallbacks,
                           public Http::ConnectionPool::Callbacks {
    UpstreamRequest(Filter& parent, Http::ConnectionPool::Instance& pool);
    ~UpstreamRequest();

    void encodeHeaders(bool end_stream);
    void encodeData(Buffer::Instance& data, bool end_stream);
    void encodeTrailers(const Http::HeaderMap& trailers);
    void resetStream();
    void setupPerTryTimeout();
    void onPerTryTimeout();

    void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
      request_info_.onUpstreamHostSelected(host);
      upstream_host_ = host;
      parent_.callbacks_->requestInfo().onUpstreamHostSelected(host);
    }

    // Http::StreamDecoder
    void decodeHeaders(Http::HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::HeaderMapPtr&& trailers) override;

    // Http::StreamCallbacks
    void onResetStream(Http::StreamResetReason reason) override;
    void onAboveWriteBufferHighWatermark() override { disableDataFromDownstream(); }
    void onBelowWriteBufferLowWatermark() override { enableDataFromDownstream(); }

    void disableDataFromDownstream() {
      parent_.cluster_->stats().upstream_flow_control_backed_up_total_.inc();
      parent_.callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    }
    void enableDataFromDownstream() {
      parent_.cluster_->stats().upstream_flow_control_drained_total_.inc();
      parent_.callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    }

    // Http::ConnectionPool::Callbacks
    void onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                       Upstream::HostDescriptionConstSharedPtr host) override;
    void onPoolReady(Http::StreamEncoder& request_encoder,
                     Upstream::HostDescriptionConstSharedPtr host) override;

    void setRequestEncoder(Http::StreamEncoder& request_encoder);
    void clearRequestEncoder();

    struct DownstreamWatermarkManager : public Http::DownstreamWatermarkCallbacks {
      DownstreamWatermarkManager(UpstreamRequest& parent) : parent_(parent) {}

      // Http::DownstreamWatermarkCallbacks
      void onBelowWriteBufferLowWatermark() override;
      void onAboveWriteBufferHighWatermark() override;

      UpstreamRequest& parent_;
    };

    void readEnable();

    Filter& parent_;
    Http::ConnectionPool::Instance& conn_pool_;
    bool grpc_rq_success_deferred_;
    Event::TimerPtr per_try_timeout_;
    Http::ConnectionPool::Cancellable* conn_pool_stream_handle_{};
    Http::StreamEncoder* request_encoder_{};
    Optional<Http::StreamResetReason> deferred_reset_reason_;
    Buffer::WatermarkBufferPtr buffered_request_body_;
    Upstream::HostDescriptionConstSharedPtr upstream_host_;
    DownstreamWatermarkManager downstream_watermark_manager_{*this};
    Tracing::SpanPtr span_;
    RequestInfo::RequestInfoImpl request_info_;
    Http::HeaderMap* upstream_headers_{};

    bool calling_encode_headers_ : 1;
    bool upstream_canary_ : 1;
    bool encode_complete_ : 1;
    bool encode_trailers_ : 1;
  };

  typedef std::unique_ptr<UpstreamRequest> UpstreamRequestPtr;

  enum class UpstreamResetType { Reset, GlobalTimeout, PerTryTimeout };

  RequestInfo::ResponseFlag streamResetReasonToResponseFlag(Http::StreamResetReason reset_reason);

  static const std::string upstreamZone(Upstream::HostDescriptionConstSharedPtr upstream_host);
  void chargeUpstreamCode(uint64_t response_status_code, const Http::HeaderMap& response_headers,
                          Upstream::HostDescriptionConstSharedPtr upstream_host, bool dropped);
  void chargeUpstreamCode(Http::Code code, Upstream::HostDescriptionConstSharedPtr upstream_host,
                          bool dropped);
  void cleanup();
  virtual RetryStatePtr createRetryState(const RetryPolicy& policy,
                                         Http::HeaderMap& request_headers,
                                         const Upstream::ClusterInfo& cluster,
                                         Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                         Event::Dispatcher& dispatcher,
                                         Upstream::ResourcePriority priority) PURE;
  Http::ConnectionPool::Instance* getConnPool();
  void maybeDoShadowing();
  void onRequestComplete();
  void onResponseTimeout();
  void onUpstreamHeaders(uint64_t response_code, Http::HeaderMapPtr&& headers, bool end_stream);
  void onUpstreamData(Buffer::Instance& data, bool end_stream);
  void onUpstreamTrailers(Http::HeaderMapPtr&& trailers);
  void onUpstreamComplete();
  void onUpstreamReset(UpstreamResetType type,
                       const Optional<Http::StreamResetReason>& reset_reason);
  void sendNoHealthyUpstreamResponse();
  bool setupRetry(bool end_stream);
  void doRetry();
  // Called immediately after a non-5xx header is received from upstream, performs stats accounting
  // and handle difference between gRPC and non-gRPC requests.
  void handleNon5xxResponseHeaders(const Http::HeaderMap& headers, bool end_stream);

  /**
   * Send a locally generated (non-proxied) HTTP response.
   * @param code supplies the HTTP status code.
   * @param body supplies the response body (empty string if no body is needed).
   * @param modify_headers supplies an optional callback function that can modify the
   *                       response headers.
   */
  void sendLocalReply(Http::Code code, const std::string& body,
                      std::function<void(Http::HeaderMap& headers)> modify_headers = nullptr);

  FilterConfig& config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_;
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  std::string alt_stat_prefix_;
  const VirtualCluster* request_vcluster_;
  Event::TimerPtr response_timeout_;
  FilterUtility::TimeoutData timeout_;
  Http::Code timeout_response_code_ = Http::Code::GatewayTimeout;
  UpstreamRequestPtr upstream_request_;
  bool grpc_request_{};
  Http::HeaderMap* downstream_headers_{};
  Http::HeaderMap* downstream_trailers_{};
  MonotonicTime downstream_request_complete_time_;
  uint32_t buffer_limit_{0};
  bool stream_destroyed_{};

  // list of cookies to add to upstream headers
  std::vector<std::string> downstream_set_cookies_;

  bool downstream_response_started_ : 1;
  bool downstream_end_stream_ : 1;
  bool do_shadowing_ : 1;
};

class ProdFilter : public Filter {
public:
  using Filter::Filter;

private:
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy& policy, Http::HeaderMap& request_headers,
                                 const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                                 Upstream::ResourcePriority priority) override;
};

} // Router
} // namespace Envoy
