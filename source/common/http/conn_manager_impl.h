#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/api_listener.h"
#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/context.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/router/rds.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/overload_manager.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/upstream.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/dump_state_utils.h"
#include "common/common/linked_object.h"
#include "common/grpc/common.h"
#include "common/http/conn_manager_config.h"
#include "common/http/filter_manager.h"
#include "common/http/user_agent.h"
#include "common/http/utility.h"
#include "common/stream_info/stream_info_impl.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

/**
 * Implementation of both ConnectionManager and ServerConnectionCallbacks. This is a
 * Network::Filter that can be installed on a connection that will perform HTTP protocol agnostic
 * handling of a connection and all requests/pushes that occur on a connection.
 */
class ConnectionManagerImpl : Logger::Loggable<Logger::Id::http>,
                              public Network::ReadFilter,
                              public ServerConnectionCallbacks,
                              public Network::ConnectionCallbacks,
                              public Http::ApiListener {
public:
  ConnectionManagerImpl(ConnectionManagerConfig& config, const Network::DrainDecision& drain_close,
                        Random::RandomGenerator& random_generator, Http::Context& http_context,
                        Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
                        Upstream::ClusterManager& cluster_manager,
                        Server::OverloadManager& overload_manager, TimeSource& time_system);
  ~ConnectionManagerImpl() override;

  static ConnectionManagerStats generateStats(const std::string& prefix, Stats::Scope& scope);
  static ConnectionManagerTracingStats generateTracingStats(const std::string& prefix,
                                                            Stats::Scope& scope);
  static void chargeTracingStats(const Tracing::Reason& tracing_reason,
                                 ConnectionManagerTracingStats& tracing_stats);
  static ConnectionManagerListenerStats generateListenerStats(const std::string& prefix,
                                                              Stats::Scope& scope);
  static const ResponseHeaderMap& continueHeader();

  // Currently the ConnectionManager creates a codec lazily when either:
  //   a) onConnection for H3.
  //   b) onData for H1 and H2.
  // With the introduction of ApiListeners, neither event occurs. This function allows consumer code
  // to manually create a codec.
  // TODO(junr03): consider passing a synthetic codec instead of creating once. The codec in the
  // ApiListener case is solely used to determine the protocol version.
  void createCodec(Buffer::Instance& data);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Http::ConnectionCallbacks
  void onGoAway(GoAwayErrorCode error_code) override;

  // Http::ServerConnectionCallbacks
  RequestDecoder& newStream(ResponseEncoder& response_encoder,
                            bool is_internally_created = false) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  // Pass connection watermark events on to all the streams associated with that connection.
  void onAboveWriteBufferHighWatermark() override {
    codec_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }
  void onBelowWriteBufferLowWatermark() override {
    codec_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

  TimeSource& timeSource() { return time_source_; }

private:
  struct ActiveStream;

  // Used to abstract making of RouteConfig update request.
  // RdsRouteConfigUpdateRequester is used when an RdsRouteConfigProvider is configured,
  // NullRouteConfigUpdateRequester is used in all other cases (specifically when
  // ScopedRdsConfigProvider/InlineScopedRoutesConfigProvider is configured)
  class RouteConfigUpdateRequester {
  public:
    virtual ~RouteConfigUpdateRequester() = default;
    virtual void requestRouteConfigUpdate(const std::string, Event::Dispatcher&,
                                          Http::RouteConfigUpdatedCallbackSharedPtr) {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    };
  };

  class RdsRouteConfigUpdateRequester : public RouteConfigUpdateRequester {
  public:
    RdsRouteConfigUpdateRequester(Router::RouteConfigProvider* route_config_provider)
        : route_config_provider_(route_config_provider) {}
    void requestRouteConfigUpdate(
        const std::string host_header, Event::Dispatcher& thread_local_dispatcher,
        Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) override;

  private:
    Router::RouteConfigProvider* route_config_provider_;
  };

  class NullRouteConfigUpdateRequester : public RouteConfigUpdateRequester {
  public:
    NullRouteConfigUpdateRequester() = default;
  };

  /**
   * Wraps a single active stream on the connection. These are either full request/response pairs
   * or pushes.
   */
  struct ActiveStream : LinkedObject<ActiveStream>,
                        public Event::DeferredDeletable,
                        public StreamCallbacks,
                        public RequestDecoder,
                        public FilterManagerCallbacks,
                        public Tracing::Config {
    ActiveStream(ConnectionManagerImpl& connection_manager);
    ~ActiveStream() override;

    // FilterManagerCallbacks
    void onSpanFinalized(Tracing::Span& span, Http::RequestHeaderMap* request_headers,
                         Http::ResponseHeaderMap* response_headers,
                         Http::ResponseTrailerMap* response_trailers) override {
      Tracing::HttpTracerUtility::finalizeDownstreamSpan(span, request_headers, response_headers,
                                                         response_trailers,
                                                         filter_manager_.streamInfo(), *this);
    }
    const Network::Connection* connection() const override;
    uint64_t streamId() const override { return stream_id_; }
    void refreshIdleTimeout() override { resetIdleTimer(); }
    void finalizeHeaders(ResponseHeaderMap& response_headers, bool end_stream) override;
    absl::optional<Router::RouteConstSharedPtr> cachedRoute() override { return cached_route_; }
    void upgradeFilterChainCreated() override {
      connection_manager_.stats_.named_.downstream_cx_upgrades_total_.inc();
      connection_manager_.stats_.named_.downstream_cx_upgrades_active_.inc();
    }
    void endStream() override {
      request_response_timespan_->complete();
      connection_manager_.doEndStream(*this);
    }
    void finalize100ContinueHeaders(ResponseHeaderMap& headers) override;
    Tracing::Config& tracingConfig() override { return *this; }
    void onFilterAboveWriteBufferHighWatermark() override {
      ENVOY_STREAM_LOG(debug, "Read-disabling downstream stream due to filter callbacks.", *this);
      response_encoder_->getStream().readDisable(true);
      connection_manager_.stats_.named_.downstream_flow_control_paused_reading_total_.inc();
    }
    void onFilterBelowWriteBufferLowWatermark() override {
      ENVOY_STREAM_LOG(debug, "Read-enabling downstream stream due to filter callbacks.", *this);
      // If the state is destroyed, the codec's stream is already torn down. On
      // teardown the codec will unwind any remaining read disable calls.
      if (!filter_manager_.state_.destroyed_) {
        response_encoder_->getStream().readDisable(false);
      }
      connection_manager_.stats_.named_.downstream_flow_control_resumed_reading_total_.inc();
    }

    virtual absl::optional<Upstream::ClusterInfoConstSharedPtr> cachedClusterInfo() override {
      return cached_cluster_info_;
    }
    void clearRouteCache() override {
      cached_route_ = absl::optional<Router::RouteConstSharedPtr>();
      cached_cluster_info_ = absl::optional<Upstream::ClusterInfoConstSharedPtr>();
      if (tracing_custom_tags_) {
        tracing_custom_tags_->clear();
      }
    }
    void refreshCachedRoute() override;
    void refreshCachedRoute(const Router::RouteCallback& cb) override;
    void recreateStream(RequestHeaderMapPtr request_headers) override {
      // n.b. we do not currently change the codecs to point at the new stream
      // decoder because the decoder callbacks are complete. It would be good to
      // null out that pointer but should not be necessary.
      ResponseEncoder* response_encoder = response_encoder_;
      response_encoder_ = nullptr;
      response_encoder->getStream().removeCallbacks(*this);
      // This functionally deletes the stream (via deferred delete) so do not
      // reference anything beyond this point.
      connection_manager_.doEndStream(*this);

      RequestDecoder& new_stream = connection_manager_.newStream(*response_encoder, true);
      // We don't need to copy over the old parent FilterState from the old StreamInfo if it did not
      // store any objects with a LifeSpan at or above DownstreamRequest. This is to avoid
      // unnecessary heap allocation.
      // TODO(snowp): In the case where connection level filter state has been set on the connection
      // FilterState that we inherit, we'll end up copying this every time even though we could get
      // away with just resetting it to the HCM filter_state_.
      if (filter_manager_.streamInfo().filterState()->hasDataAtOrAboveLifeSpan(
              StreamInfo::FilterState::LifeSpan::Request)) {
        // TODO something needs to be done here so we can reset the FilterState
        // (*connection_manager_.streams_.begin())->filter_manager_.streamInfo().stream_info_.filter_state_
        // =
        //     std::make_shared<StreamInfo::FilterStateImpl>(
        //         parent_.stream_info_.filter_state_->parent(),
        //         StreamInfo::FilterState::LifeSpan::FilterChain);
      }

      new_stream.decodeHeaders(std::move(request_headers), true);
    }
    void requestRouteConfigUpdate(
        Event::Dispatcher& thread_local_dispatcher,
        Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) override;

    absl::optional<Router::ConfigConstSharedPtr> routeConfig() override;

    void chargeStats(const ResponseHeaderMap& headers);

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason reason,
                       absl::string_view transport_failure_reason) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

    // Http::StreamDecoder
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeMetadata(MetadataMapPtr&&) override;

    // Http::RequestDecoder
    void decodeHeaders(RequestHeaderMapPtr&& headers, bool end_stream) override;
    void decodeTrailers(RequestTrailerMapPtr&& trailers) override;
    void sendLocalReply(bool is_grpc_request, Code code, absl::string_view body,
                        const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
                        bool is_head_request,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                        absl::string_view details) override {
      filter_manager_.sendLocalReply(is_grpc_request, code, body, modify_headers, is_head_request,
                                     grpc_status, details);
    }

    // Tracing::TracingConfig
    Tracing::OperationName operationName() const override;
    const Tracing::CustomTagMap* customTags() const override;
    bool verbose() const override;
    uint32_t maxPathTagLength() const override;

    void traceRequest();

    // Updates the snapped_route_config_ (by reselecting scoped route configuration), if a scope is
    // not found, snapped_route_config_ is set to Router::NullConfigImpl.
    void snapScopedRouteConfig();

    void refreshCachedTracingCustomTags();

    // All state for the stream. Put here for readability.
    struct State {
      State() : saw_connection_close_(false) {}

      // The following 3 members are booleans rather than part of the space-saving bitfield as they
      // are passed as arguments to functions expecting bools. Extend State using the bitfield
      // where possible.
      bool saw_connection_close_ : 1;

      // True if this stream is internally created. Currently only used for
      // internal redirects or other streams created via recreateStream().
      bool is_internally_created_ : 1;
    };

    // Per-stream idle timeout callback.
    void onIdleTimeout();
    // Reset per-stream idle timer.
    void resetIdleTimer();
    // Per-stream request timeout callback.
    void onRequestTimeout();
    // Per-stream alive duration reached.
    void onStreamMaxDurationReached();
    bool hasCachedRoute() { return cached_route_.has_value() && cached_route_.value(); }

    // Return local port of the connection.
    uint32_t localPort();

    friend std::ostream& operator<<(std::ostream& os, const ActiveStream& s) {
      s.filter_manager_.dumpState(os);
      return os;
    }

    Tracing::CustomTagMap& getOrMakeTracingCustomTagMap() {
      if (tracing_custom_tags_ == nullptr) {
        tracing_custom_tags_ = std::make_unique<Tracing::CustomTagMap>();
      }
      return *tracing_custom_tags_;
    }

    ConnectionManagerImpl& connection_manager_;
    Router::ConfigConstSharedPtr snapped_route_config_;
    Router::ScopedConfigConstSharedPtr snapped_scoped_routes_config_;
    const uint64_t stream_id_;
    std::unique_ptr<Tracing::CustomTagMap> tracing_custom_tags_{nullptr};
    FilterManager filter_manager_;
    Stats::TimespanPtr request_response_timespan_;
    // Per-stream idle timeout.
    Event::TimerPtr stream_idle_timer_;
    // Per-stream alive duration.
    Event::TimerPtr max_stream_duration_timer_;
    std::chrono::milliseconds idle_timeout_ms_{};
    State state_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    absl::optional<Upstream::ClusterInfoConstSharedPtr> cached_cluster_info_;
    ResponseEncoder* response_encoder_;
    std::unique_ptr<RouteConfigUpdateRequester> route_config_update_requester_;
  };

  using ActiveStreamPtr = std::unique_ptr<ActiveStream>;

  /**
   * Check to see if the connection can be closed after gracefully waiting to send pending codec
   * data.
   */
  void checkForDeferredClose();

  /**
   * Do a delayed destruction of a stream to allow for stack unwind. Also calls onDestroy() for
   * each filter.
   */
  void doDeferredStreamDestroy(ActiveStream& stream);

  /**
   * Process a stream that is ending due to upstream response or reset.
   */
  void doEndStream(ActiveStream& stream);

  void resetAllStreams(absl::optional<StreamInfo::ResponseFlag> response_flag);
  void onIdleTimeout();
  void onConnectionDurationTimeout();
  void onDrainTimeout();
  void startDrainSequence();
  Tracing::HttpTracer& tracer() { return *config_.tracer(); }
  void handleCodecError(absl::string_view error);
  void doConnectionClose(absl::optional<Network::ConnectionCloseType> close_type,
                         absl::optional<StreamInfo::ResponseFlag> response_flag);

  enum class DrainState { NotDraining, Draining, Closing };

  ConnectionManagerConfig& config_;
  ConnectionManagerStats& stats_; // We store a reference here to avoid an extra stats() call on the
                                  // config in the hot path.
  ServerConnectionPtr codec_;
  std::list<ActiveStreamPtr> streams_;
  Stats::TimespanPtr conn_length_;
  const Network::DrainDecision& drain_close_;
  DrainState drain_state_{DrainState::NotDraining};
  UserAgent user_agent_;
  // An idle timer for the connection. This is only armed when there are no streams on the
  // connection. When there are active streams it is disarmed in favor of each stream's
  // stream_idle_timer_.
  Event::TimerPtr connection_idle_timer_;
  // A connection duration timer. Armed during handling new connection if enabled in config.
  Event::TimerPtr connection_duration_timer_;
  Event::TimerPtr drain_timer_;
  Random::RandomGenerator& random_generator_;
  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  ConnectionManagerListenerStats& listener_stats_;
  // References into the overload manager thread local state map. Using these lets us avoid a map
  // lookup in the hot path of processing each request.
  const Server::OverloadActionState& overload_stop_accepting_requests_ref_;
  const Server::OverloadActionState& overload_disable_keepalive_ref_;
  TimeSource& time_source_;
};

} // namespace Http
} // namespace Envoy
