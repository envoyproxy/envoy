#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/http/filter.h"
#include "envoy/http/websocket.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/router/rds.h"
#include "envoy/runtime/runtime.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/upstream.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/linked_object.h"
#include "common/http/date_provider.h"
#include "common/http/user_agent.h"
#include "common/http/websocket/ws_handler_impl.h"
#include "common/request_info/request_info_impl.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

/**
 * All stats for the connection manager. @see stats_macros.h
 */
// clang-format off
#define ALL_HTTP_CONN_MAN_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER  (downstream_cx_total)                                                                   \
  COUNTER  (downstream_cx_ssl_total)                                                               \
  COUNTER  (downstream_cx_http1_total)                                                             \
  COUNTER  (downstream_cx_websocket_total)                                                         \
  COUNTER  (downstream_cx_http2_total)                                                             \
  COUNTER  (downstream_cx_destroy)                                                                 \
  COUNTER  (downstream_cx_destroy_remote)                                                          \
  COUNTER  (downstream_cx_destroy_local)                                                           \
  COUNTER  (downstream_cx_destroy_active_rq)                                                       \
  COUNTER  (downstream_cx_destroy_local_active_rq)                                                 \
  COUNTER  (downstream_cx_destroy_remote_active_rq)                                                \
  GAUGE    (downstream_cx_active)                                                                  \
  GAUGE    (downstream_cx_ssl_active)                                                              \
  GAUGE    (downstream_cx_http1_active)                                                            \
  GAUGE    (downstream_cx_websocket_active)                                                        \
  GAUGE    (downstream_cx_http2_active)                                                            \
  COUNTER  (downstream_cx_protocol_error)                                                          \
  HISTOGRAM(downstream_cx_length_ms)                                                               \
  COUNTER  (downstream_cx_rx_bytes_total)                                                          \
  GAUGE    (downstream_cx_rx_bytes_buffered)                                                       \
  COUNTER  (downstream_cx_tx_bytes_total)                                                          \
  GAUGE    (downstream_cx_tx_bytes_buffered)                                                       \
  COUNTER  (downstream_cx_drain_close)                                                             \
  COUNTER  (downstream_cx_idle_timeout)                                                            \
  COUNTER  (downstream_flow_control_paused_reading_total)                                          \
  COUNTER  (downstream_flow_control_resumed_reading_total)                                         \
  COUNTER  (downstream_rq_total)                                                                   \
  COUNTER  (downstream_rq_http1_total)                                                             \
  COUNTER  (downstream_rq_http2_total)                                                             \
  GAUGE    (downstream_rq_active)                                                                  \
  COUNTER  (downstream_rq_response_before_rq_complete)                                             \
  COUNTER  (downstream_rq_rx_reset)                                                                \
  COUNTER  (downstream_rq_tx_reset)                                                                \
  COUNTER  (downstream_rq_non_relative_path)                                                       \
  COUNTER  (downstream_rq_ws_on_non_ws_route)                                                      \
  COUNTER  (downstream_rq_too_large)                                                               \
  COUNTER  (downstream_rq_1xx)                                                                     \
  COUNTER  (downstream_rq_2xx)                                                                     \
  COUNTER  (downstream_rq_3xx)                                                                     \
  COUNTER  (downstream_rq_4xx)                                                                     \
  COUNTER  (downstream_rq_5xx)                                                                     \
  HISTOGRAM(downstream_rq_time)                                                                    \
  COUNTER  (rs_too_large)
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct ConnectionManagerNamedStats {
  ALL_HTTP_CONN_MAN_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

struct ConnectionManagerStats {
  ConnectionManagerNamedStats named_;
  std::string prefix_;
  Stats::Scope& scope_;
};

/**
 * Connection manager tracing specific stats. @see stats_macros.h
 */
// clang-format off
#define CONN_MAN_TRACING_STATS(COUNTER)                                                            \
  COUNTER(random_sampling)                                                                         \
  COUNTER(service_forced)                                                                          \
  COUNTER(client_enabled)                                                                          \
  COUNTER(not_traceable)                                                                           \
  COUNTER(health_check)
// clang-format on

/**
 * Wrapper struct for connection manager tracing stats. @see stats_macros.h
 */
struct ConnectionManagerTracingStats {
  CONN_MAN_TRACING_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for tracing which is set on the connection manager level.
 * Http Tracing can be enabled/disabled on a per connection manager basis.
 * Here we specify some specific for connection manager settings.
 */
struct TracingConnectionManagerConfig {
  Tracing::OperationName operation_name_;
  std::vector<Http::LowerCaseString> request_headers_for_tags_;
};

typedef std::unique_ptr<TracingConnectionManagerConfig> TracingConnectionManagerConfigPtr;

/**
 * Connection manager per listener stats. @see stats_macros.h
 */
// clang-format off
#define CONN_MAN_LISTENER_STATS(COUNTER)                                                           \
  COUNTER(downstream_rq_1xx)                                                                       \
  COUNTER(downstream_rq_2xx)                                                                       \
  COUNTER(downstream_rq_3xx)                                                                       \
  COUNTER(downstream_rq_4xx)                                                                       \
  COUNTER(downstream_rq_5xx)
// clang-format on

/**
 * Wrapper struct for connection manager listener stats. @see stats_macros.h
 */
struct ConnectionManagerListenerStats {
  CONN_MAN_LISTENER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for how to forward client certs.
 */
enum class ForwardClientCertType {
  ForwardOnly,
  AppendForward,
  SanitizeSet,
  Sanitize,
  AlwaysForwardOnly
};

/**
 * Configuration for the fields of the client cert, used for populating the current client cert
 * information to the next hop.
 */
enum class ClientCertDetailsType { Cert, Subject, SAN };

/**
 * Abstract configuration for the connection manager.
 */
class ConnectionManagerConfig {
public:
  virtual ~ConnectionManagerConfig() {}

  /**
   *  @return const std::list<AccessLog::InstanceSharedPtr>& the access logs to write to.
   */
  virtual const std::list<AccessLog::InstanceSharedPtr>& accessLogs() PURE;

  /**
   * Called to create a codec for the connection manager. This function will be called when the
   * first byte of application data is received. This is done to support handling of ALPN, protocol
   * detection, etc.
   * @param connection supplies the owning connection.
   * @param data supplies the currently available read data.
   * @param callbacks supplies the callbacks to install into the codec.
   * @return a codec or nullptr if no codec can be created.
   */
  virtual ServerConnectionPtr createCodec(Network::Connection& connection,
                                          const Buffer::Instance& data,
                                          ServerConnectionCallbacks& callbacks) PURE;

  /**
   * @return DateProvider& the date provider to use for
   */
  virtual DateProvider& dateProvider() PURE;

  /**
   * @return the time in milliseconds the connection manager will wait betwen issuing a "shutdown
   *         notice" to the time it will issue a full GOAWAY and not accept any new streams.
   */
  virtual std::chrono::milliseconds drainTimeout() PURE;

  /**
   * @return FilterChainFactory& the HTTP level filter factory to build the connection's filter
   *         chain.
   */
  virtual FilterChainFactory& filterFactory() PURE;

  /**
   * @return whether the connection manager will generate a fresh x-request-id if the request does
   *         not have one.
   */
  virtual bool generateRequestId() PURE;

  /**
   * @return optional idle timeout for incoming connection manager connections.
   */
  virtual const Optional<std::chrono::milliseconds>& idleTimeout() PURE;

  /**
   * @return Router::RouteConfigProvider& the configuration provider used to acquire a route
   *         config for each request flow.
   */
  virtual Router::RouteConfigProvider& routeConfigProvider() PURE;

  /**
   * @return const std::string& the server name to write into responses.
   */
  virtual const std::string& serverName() PURE;

  /**
   * @return ConnectionManagerStats& the stats to write to.
   */
  virtual ConnectionManagerStats& stats() PURE;

  /**
   * @return ConnectionManagerTracingStats& the stats to write to.
   */
  virtual ConnectionManagerTracingStats& tracingStats() PURE;

  /**
   * @return bool whether to use the remote address for populating XFF, determining internal request
   *         status, etc. or to assume that XFF will already be populated with the remote address.
   */
  virtual bool useRemoteAddress() PURE;

  /**
   * @return uint32_t the number of trusted proxy hops in front of this Envoy instance, for
   *         the purposes of XFF processing.
   */
  virtual uint32_t xffNumTrustedHops() const PURE;

  /**
   * @return ForwardClientCertType the configuration of how to forward the client cert information.
   */
  virtual ForwardClientCertType forwardClientCert() PURE;

  /**
   * @return vector of ClientCertDetailsType the configuration of the current client cert's details
   * to be forwarded.
   */
  virtual const std::vector<ClientCertDetailsType>& setCurrentClientCertDetails() const PURE;

  /**
   * @return local address.
   * Gives richer information in case of internal requests.
   */
  virtual const Network::Address::Instance& localAddress() PURE;

  /**
   * @return custom user agent for internal requests for better debugging. Must be configured to
   *         be enabled. User agent will only overwritten if it doesn't already exist. If enabled,
   *         the same user agent will be written to the x-envoy-downstream-service-cluster header.
   */
  virtual const Optional<std::string>& userAgent() PURE;

  /**
   * @return tracing config.
   */
  virtual const TracingConnectionManagerConfig* tracingConfig() PURE;

  /**
   * @return ConnectionManagerListenerStats& the stats to write to.
   */
  virtual ConnectionManagerListenerStats& listenerStats() PURE;

  /**
   * @return bool supplies if the HttpConnectionManager should proxy the Expect: 100-Continue
   */
  virtual bool proxy100Continue() const PURE;
};

/**
 * Implementation of both ConnectionManager and ServerConnectionCallbacks. This is a
 * Network::Filter that can be installed on a connection that will perform HTTP protocol agnostic
 * handling of a connection and all requests/pushes that occur on a connection.
 */
class ConnectionManagerImpl : Logger::Loggable<Logger::Id::http>,
                              public Network::ReadFilter,
                              public ServerConnectionCallbacks,
                              public Network::ConnectionCallbacks {
public:
  ConnectionManagerImpl(ConnectionManagerConfig& config, const Network::DrainDecision& drain_close,
                        Runtime::RandomGenerator& random_generator, Tracing::HttpTracer& tracer,
                        Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
                        Upstream::ClusterManager& cluster_manager);
  ~ConnectionManagerImpl();

  static ConnectionManagerStats generateStats(const std::string& prefix, Stats::Scope& scope);
  static ConnectionManagerTracingStats generateTracingStats(const std::string& prefix,
                                                            Stats::Scope& scope);
  static void chargeTracingStats(const Tracing::Reason& tracing_reason,
                                 ConnectionManagerTracingStats& tracing_stats);
  static ConnectionManagerListenerStats generateListenerStats(const std::string& prefix,
                                                              Stats::Scope& scope);
  static const HeaderMapImpl& continueHeader();

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Http::ConnectionCallbacks
  void onGoAway() override;

  // Http::ServerConnectionCallbacks
  StreamDecoder& newStream(StreamEncoder& response_encoder) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  // Pass connection watermark events on to all the streams associated with that connection.
  void onAboveWriteBufferHighWatermark() override {
    codec_->onUnderlyingConnectionAboveWriteBufferHighWatermark();
  }
  void onBelowWriteBufferLowWatermark() override {
    codec_->onUnderlyingConnectionBelowWriteBufferLowWatermark();
  }

private:
  struct ActiveStream;

  /**
   * Base class wrapper for both stream encoder and decoder filters.
   */
  struct ActiveStreamFilterBase : public virtual StreamFilterCallbacks {
    ActiveStreamFilterBase(ActiveStream& parent, bool dual_filter)
        : parent_(parent), headers_continued_(false), continue_headers_continued_(false),
          stopped_(false), dual_filter_(dual_filter) {}

    bool commonHandleAfter100ContinueHeadersCallback(FilterHeadersStatus status);
    bool commonHandleAfterHeadersCallback(FilterHeadersStatus status);
    void commonHandleBufferData(Buffer::Instance& provided_data);
    bool commonHandleAfterDataCallback(FilterDataStatus status, Buffer::Instance& provided_data,
                                       bool& buffer_was_streaming);
    bool commonHandleAfterTrailersCallback(FilterTrailersStatus status);

    void commonContinue();
    virtual bool canContinue() PURE;
    virtual Buffer::WatermarkBufferPtr createBuffer() PURE;
    virtual Buffer::WatermarkBufferPtr& bufferedData() PURE;
    virtual bool complete() PURE;
    virtual void do100ContinueHeaders() PURE;
    virtual void doHeaders(bool end_stream) PURE;
    virtual void doData(bool end_stream) PURE;
    virtual void doTrailers() PURE;
    virtual const HeaderMapPtr& trailers() PURE;

    // Http::StreamFilterCallbacks
    const Network::Connection* connection() override;
    Event::Dispatcher& dispatcher() override;
    void resetStream() override;
    Router::RouteConstSharedPtr route() override;
    void clearRouteCache() override;
    uint64_t streamId() override;
    RequestInfo::RequestInfo& requestInfo() override;
    Tracing::Span& activeSpan() override;
    Tracing::Config& tracingConfig() override;

    ActiveStream& parent_;
    bool headers_continued_ : 1;
    bool continue_headers_continued_ : 1;
    bool stopped_ : 1;
    const bool dual_filter_ : 1;
  };

  /**
   * Wrapper for a stream decoder filter.
   */
  struct ActiveStreamDecoderFilter : public ActiveStreamFilterBase,
                                     public StreamDecoderFilterCallbacks,
                                     LinkedObject<ActiveStreamDecoderFilter> {
    ActiveStreamDecoderFilter(ActiveStream& parent, StreamDecoderFilterSharedPtr filter,
                              bool dual_filter)
        : ActiveStreamFilterBase(parent, dual_filter), handle_(filter) {}

    // ActiveStreamFilterBase
    bool canContinue() override {
      // It is possible for the connection manager to respond directly to a request even while
      // a filter is trying to continue. If a response has already happened, we should not
      // continue to further filters. A concrete example of this is a filter buffering data, the
      // last data frame comes in and the filter continues, but the final buffering takes the stream
      // over the high watermark such that a 413 is returned.
      return !parent_.state_.local_complete_;
    }
    Buffer::WatermarkBufferPtr createBuffer() override;
    Buffer::WatermarkBufferPtr& bufferedData() override { return parent_.buffered_request_data_; }
    bool complete() override { return parent_.state_.remote_complete_; }
    void do100ContinueHeaders() override { NOT_REACHED; }
    void doHeaders(bool end_stream) override {
      parent_.decodeHeaders(this, *parent_.request_headers_, end_stream);
    }
    void doData(bool end_stream) override {
      parent_.decodeData(this, *parent_.buffered_request_data_, end_stream);
    }
    void doTrailers() override { parent_.decodeTrailers(this, *parent_.request_trailers_); }
    const HeaderMapPtr& trailers() override { return parent_.request_trailers_; }

    // Http::StreamDecoderFilterCallbacks
    void addDecodedData(Buffer::Instance& data, bool streaming) override;
    void continueDecoding() override;
    const Buffer::Instance* decodingBuffer() override {
      return parent_.buffered_request_data_.get();
    }
    void encode100ContinueHeaders(HeaderMapPtr&& headers) override;
    void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(HeaderMapPtr&& trailers) override;
    void onDecoderFilterAboveWriteBufferHighWatermark() override;
    void onDecoderFilterBelowWriteBufferLowWatermark() override;
    void
    addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& watermark_callbacks) override;
    void
    removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& watermark_callbacks) override;
    void setDecoderBufferLimit(uint32_t limit) override { parent_.setBufferLimit(limit); }
    uint32_t decoderBufferLimit() override { return parent_.buffer_limit_; }

    void requestDataTooLarge();
    void requestDataDrained();

    StreamDecoderFilterSharedPtr handle_;
  };

  typedef std::unique_ptr<ActiveStreamDecoderFilter> ActiveStreamDecoderFilterPtr;

  /**
   * Wrapper for a stream encoder filter.
   */
  struct ActiveStreamEncoderFilter : public ActiveStreamFilterBase,
                                     public StreamEncoderFilterCallbacks,
                                     LinkedObject<ActiveStreamEncoderFilter> {
    ActiveStreamEncoderFilter(ActiveStream& parent, StreamEncoderFilterSharedPtr filter,
                              bool dual_filter)
        : ActiveStreamFilterBase(parent, dual_filter), handle_(filter) {}

    // ActiveStreamFilterBase
    bool canContinue() override { return true; }
    Buffer::WatermarkBufferPtr createBuffer() override;
    Buffer::WatermarkBufferPtr& bufferedData() override { return parent_.buffered_response_data_; }
    bool complete() override { return parent_.state_.local_complete_; }
    void do100ContinueHeaders() override {
      parent_.encode100ContinueHeaders(this, *parent_.continue_headers_);
    }
    void doHeaders(bool end_stream) override {
      parent_.encodeHeaders(this, *parent_.response_headers_, end_stream);
    }
    void doData(bool end_stream) override {
      parent_.encodeData(this, *parent_.buffered_response_data_, end_stream);
    }
    void doTrailers() override { parent_.encodeTrailers(this, *parent_.response_trailers_); }
    const HeaderMapPtr& trailers() override { return parent_.response_trailers_; }

    // Http::StreamEncoderFilterCallbacks
    void addEncodedData(Buffer::Instance& data, bool streaming) override;
    void onEncoderFilterAboveWriteBufferHighWatermark() override;
    void onEncoderFilterBelowWriteBufferLowWatermark() override;
    void setEncoderBufferLimit(uint32_t limit) override { parent_.setBufferLimit(limit); }
    uint32_t encoderBufferLimit() override { return parent_.buffer_limit_; }
    void continueEncoding() override;
    const Buffer::Instance* encodingBuffer() override {
      return parent_.buffered_response_data_.get();
    }

    void responseDataTooLarge();
    void responseDataDrained();

    StreamEncoderFilterSharedPtr handle_;
  };

  typedef std::unique_ptr<ActiveStreamEncoderFilter> ActiveStreamEncoderFilterPtr;

  /**
   * Wraps a single active stream on the connection. These are either full request/response pairs
   * or pushes.
   */
  struct ActiveStream : LinkedObject<ActiveStream>,
                        public Event::DeferredDeletable,
                        public StreamCallbacks,
                        public StreamDecoder,
                        public FilterChainFactoryCallbacks,
                        public WsHandlerCallbacks,
                        public Tracing::Config {
    ActiveStream(ConnectionManagerImpl& connection_manager);
    ~ActiveStream();

    void addStreamDecoderFilterWorker(StreamDecoderFilterSharedPtr filter, bool dual_filter);
    void addStreamEncoderFilterWorker(StreamEncoderFilterSharedPtr filter, bool dual_filter);
    void chargeStats(const HeaderMap& headers);
    std::list<ActiveStreamEncoderFilterPtr>::iterator
    commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream);
    uint64_t connectionId();
    const Network::Connection* connection();
    Ssl::Connection* ssl();
    void addDecodedData(ActiveStreamDecoderFilter& filter, Buffer::Instance& data, bool streaming);
    void decodeHeaders(ActiveStreamDecoderFilter* filter, HeaderMap& headers, bool end_stream);
    void decodeData(ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream);
    void decodeTrailers(ActiveStreamDecoderFilter* filter, HeaderMap& trailers);
    void addEncodedData(ActiveStreamEncoderFilter& filter, Buffer::Instance& data, bool streaming);
    void encode100ContinueHeaders(ActiveStreamEncoderFilter* filter, HeaderMap& headers);
    void encodeHeaders(ActiveStreamEncoderFilter* filter, HeaderMap& headers, bool end_stream);
    void encodeData(ActiveStreamEncoderFilter* filter, Buffer::Instance& data, bool end_stream);
    void encodeTrailers(ActiveStreamEncoderFilter* filter, HeaderMap& trailers);
    void maybeEndEncode(bool end_stream);
    uint64_t streamId() { return stream_id_; }

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason reason) override;
    void onAboveWriteBufferHighWatermark() override;
    void onBelowWriteBufferLowWatermark() override;

    // Http::StreamDecoder
    void decode100ContinueHeaders(HeaderMapPtr&&) override { NOT_REACHED; }
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(HeaderMapPtr&& trailers) override;

    // Http::FilterChainFactoryCallbacks
    void addStreamDecoderFilter(StreamDecoderFilterSharedPtr filter) override {
      addStreamDecoderFilterWorker(filter, false);
    }
    void addStreamEncoderFilter(StreamEncoderFilterSharedPtr filter) override {
      addStreamEncoderFilterWorker(filter, false);
    }
    void addStreamFilter(StreamFilterSharedPtr filter) override {
      addStreamDecoderFilterWorker(filter, true);
      addStreamEncoderFilterWorker(filter, true);
    }
    void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) override;

    // Http::WsHandlerCallbacks
    void sendHeadersOnlyResponse(HeaderMap& headers) override {
      encodeHeaders(nullptr, headers, true);
    }

    // Tracing::TracingConfig
    virtual Tracing::OperationName operationName() const override;
    virtual const std::vector<Http::LowerCaseString>& requestHeadersForTags() const override;

    void traceRequest();

    void refreshCachedRoute();

    // Pass on watermark callbacks to watermark subscribers. This boils down to passing watermark
    // events for this stream and the downstream connection to the router filter.
    void callHighWatermarkCallbacks();
    void callLowWatermarkCallbacks();

    /**
     * Flags that keep track of which filter calls are currently in progress.
     */
    // clang-format off
    struct FilterCallState {
      static constexpr uint32_t DecodeHeaders   = 0x01;
      static constexpr uint32_t DecodeData      = 0x02;
      static constexpr uint32_t DecodeTrailers  = 0x04;
      static constexpr uint32_t EncodeHeaders   = 0x08;
      static constexpr uint32_t EncodeData      = 0x10;
      static constexpr uint32_t EncodeTrailers  = 0x20;
      // Encode100ContinueHeaders is a bit of a special state as 100 continue
      // headers may be sent during request processing. This state is only used
      // to verify we do not encode100Continue headers more than once per
      // filter.
      static constexpr uint32_t Encode100ContinueHeaders  = 0x40;
    };
    // clang-format on

    // All state for the stream. Put here for readability.
    struct State {
      State() : remote_complete_(false), local_complete_(false), saw_connection_close_(false) {}

      uint32_t filter_call_state_{0};
      // The following 3 members are booleans rather than part of the space-saving bitfield as they
      // are passed as arguments to functions expecting bools. Extend State using the bitfield
      // where possible.
      bool encoder_filters_streaming_{true};
      bool decoder_filters_streaming_{true};
      bool destroyed_{false};
      bool remote_complete_ : 1;
      bool local_complete_ : 1;
      bool saw_connection_close_ : 1;
    };

    // Possibly increases buffer_limit_ to the value of limit.
    void setBufferLimit(uint32_t limit);

    ConnectionManagerImpl& connection_manager_;
    Router::ConfigConstSharedPtr snapped_route_config_;
    Tracing::SpanPtr active_span_;
    const uint64_t stream_id_;
    StreamEncoder* response_encoder_{};
    HeaderMapPtr continue_headers_;
    HeaderMapPtr response_headers_;
    Buffer::WatermarkBufferPtr buffered_response_data_;
    HeaderMapPtr response_trailers_{};
    HeaderMapPtr request_headers_;
    Buffer::WatermarkBufferPtr buffered_request_data_;
    HeaderMapPtr request_trailers_;
    std::list<ActiveStreamDecoderFilterPtr> decoder_filters_;
    std::list<ActiveStreamEncoderFilterPtr> encoder_filters_;
    std::list<AccessLog::InstanceSharedPtr> access_log_handlers_;
    Stats::TimespanPtr request_timer_;
    State state_;
    RequestInfo::RequestInfoImpl request_info_;
    Optional<Router::RouteConstSharedPtr> cached_route_;
    DownstreamWatermarkCallbacks* watermark_callbacks_{nullptr};
    uint32_t buffer_limit_{0};
    uint32_t high_watermark_count_{0};
    const std::string* decorated_operation_{nullptr};
    // By default, we will assume there are no 100-Continue headers. If encode100ContinueHeaders
    // is ever called, this is set to true so commonContinue resumes processing the 100-Continue.
    bool has_continue_headers_{};
  };

  typedef std::unique_ptr<ActiveStream> ActiveStreamPtr;

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

  void resetAllStreams();
  void onIdleTimeout();
  void onDrainTimeout();
  void startDrainSequence();

  bool isWebSocketConnection() const { return ws_connection_ != nullptr; }

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
  Event::TimerPtr idle_timer_;
  Event::TimerPtr drain_timer_;
  Runtime::RandomGenerator& random_generator_;
  Tracing::HttpTracer& tracer_;
  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  Upstream::ClusterManager& cluster_manager_;
  WebSocket::WsHandlerImplPtr ws_connection_{};
  Network::ReadFilterCallbacks* read_callbacks_{};
  ConnectionManagerListenerStats& listener_stats_;
};

} // Http
} // namespace Envoy
