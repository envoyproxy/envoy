#pragma once

#include "user_agent.h"

#include "envoy/event/deferred_deletable.h"
#include "envoy/http/access_log.h"
#include "envoy/http/codec.h"
#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/common/utility.h"
#include "common/http/access_log/request_info_impl.h"

namespace Http {

/**
 * All stats for the connection manager. @see stats_macros.h
 */
// clang-format off
#define ALL_HTTP_CONN_MAN_STATS(COUNTER, GAUGE, TIMER)                                             \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_ssl_total)                                                                 \
  COUNTER(downstream_cx_http1_total)                                                               \
  COUNTER(downstream_cx_http2_total)                                                               \
  COUNTER(downstream_cx_destroy)                                                                   \
  COUNTER(downstream_cx_destroy_remote)                                                            \
  COUNTER(downstream_cx_destroy_local)                                                             \
  COUNTER(downstream_cx_destroy_active_rq)                                                         \
  COUNTER(downstream_cx_destroy_local_active_rq)                                                   \
  COUNTER(downstream_cx_destroy_remote_active_rq)                                                  \
  GAUGE  (downstream_cx_active)                                                                    \
  GAUGE  (downstream_cx_ssl_active)                                                                \
  GAUGE  (downstream_cx_http1_active)                                                              \
  GAUGE  (downstream_cx_http2_active)                                                              \
  COUNTER(downstream_cx_protocol_error)                                                            \
  TIMER  (downstream_cx_length_ms)                                                                 \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  GAUGE  (downstream_cx_rx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  GAUGE  (downstream_cx_tx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_drain_close)                                                               \
  COUNTER(downstream_cx_idle_timeout)                                                              \
  COUNTER(downstream_rq_total)                                                                     \
  COUNTER(downstream_rq_http1_total)                                                               \
  COUNTER(downstream_rq_http2_total)                                                               \
  GAUGE  (downstream_rq_active)                                                                    \
  COUNTER(downstream_rq_response_before_rq_complete)                                               \
  COUNTER(downstream_rq_rx_reset)                                                                  \
  COUNTER(downstream_rq_tx_reset)                                                                  \
  COUNTER(downstream_rq_non_relative_path)                                                         \
  COUNTER(downstream_rq_2xx)                                                                       \
  COUNTER(downstream_rq_3xx)                                                                       \
  COUNTER(downstream_rq_4xx)                                                                       \
  COUNTER(downstream_rq_5xx)                                                                       \
  TIMER  (downstream_rq_time)                                                                      \
  COUNTER(failed_generate_uuid)
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct ConnectionManagerNamedStats {
  ALL_HTTP_CONN_MAN_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
};

struct ConnectionManagerStats {
  ConnectionManagerNamedStats named_;
  std::string prefix_;
  Stats::Store& store_;
};

enum class TracingType {
  // Trace all traceable requests.
  All,
  // Trace only when there is an upstream failure reason.
  UpstreamFailure
};

/**
 * Configuration for tracing which is set on the connection manager level.
 * Http Tracing can be enabled/disabled on a per connection manager basis.
 * Here we specify some specific for connection manager settings.
 */
struct TracingConnectionManagerConfig {
  std::string operation_name_;
  TracingType tracing_type_;
};

/**
 * Abstract configuration for the connection manager.
 */
class ConnectionManagerConfig {
public:
  virtual ~ConnectionManagerConfig() {}

  /**
   *  @return const std::list<AccessLog::InstancePtr>& the access logs to write to.
   */
  virtual const std::list<AccessLog::InstancePtr>& accessLogs() PURE;

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
   * @return const Router::Config& the route configuration for all connection manager requests.
   */
  virtual const Router::Config& routeConfig() PURE;

  /**
   * @return const std::string& the server name to write into responses.
   */
  virtual const std::string& serverName() PURE;

  /**
   * @return ConnectionManagerStats& the stats to write to.
   */
  virtual ConnectionManagerStats& stats() PURE;

  /**
   * @return bool whether to use the remote address for populating XFF, determining internal request
   *         status, etc. or to assume that XFF will already be populated with the remote address.
   */
  virtual bool useRemoteAddress() PURE;

  /**
   * @return local address.
   * Gives richer information in case of internal requests.
   */
  virtual const std::string& localAddress() PURE;

  /**
   * @return custom user agent for internal requests for better debugging. Must be configured to
   *         be enabled. User agent will only overwritten if it doesn't already exist. If enabled,
   *         the same user agent will be written to the x-envoy-downstream-service-cluster header.
   */
  virtual const Optional<std::string>& userAgent() PURE;

  /**
   * @return tracing config.
   */
  virtual const Optional<TracingConnectionManagerConfig>& tracingConfig() PURE;
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
  ConnectionManagerImpl(ConnectionManagerConfig& config, Network::DrainDecision& drain_close,
                        Runtime::RandomGenerator& random_generator, Tracing::HttpTracer& tracer,
                        Runtime::Loader& runtime);
  ~ConnectionManagerImpl();

  static ConnectionManagerStats generateStats(const std::string& prefix, Stats::Store& stats);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Http::ConnectionCallbacks
  void onGoAway() override;

  // Http::ServerConnectionCallbacks
  StreamDecoder& newStream(StreamEncoder& response_encoder) override;

  // Network::ConnectionCallbacks
  void onBufferChange(Network::ConnectionBufferType type, uint64_t old_size,
                      int64_t delta) override;
  void onEvent(uint32_t events) override;

private:
  struct ActiveStream;

  /**
   * Base class wrapper for both stream encoder and decoder filters.
   */
  struct ActiveStreamFilterBase : public virtual StreamFilterCallbacks,
                                  public Router::StableRouteTable {
    ActiveStreamFilterBase(ActiveStream& parent) : parent_(parent) {}

    bool commonHandleAfterHeadersCallback(FilterHeadersStatus status);
    void commonHandleBufferData(Buffer::Instance& provided_data);
    bool commonHandleAfterDataCallback(FilterDataStatus status, Buffer::Instance& provided_data);
    bool commonHandleAfterTrailersCallback(FilterTrailersStatus status);

    void commonContinue();
    virtual Buffer::InstancePtr& bufferedData() PURE;
    virtual bool complete() PURE;
    virtual void doHeaders(bool end_stream) PURE;
    virtual void doData(bool end_stream) PURE;
    virtual void doTrailers() PURE;
    virtual const HeaderMapPtr& trailers() PURE;

    // Http::StreamFilterCallbacks
    void addResetStreamCallback(std::function<void()> callback) override;
    uint64_t connectionId() override;
    Event::Dispatcher& dispatcher() override;
    void resetStream() override;
    const Router::StableRouteTable& routeTable() override { return *this; }
    uint64_t streamId() override;
    AccessLog::RequestInfo& requestInfo() override;
    const std::string& downstreamAddress() override;

    // Router::StableRouteTable
    const Router::RedirectEntry* redirectRequest(const HeaderMap& headers) const {
      return parent_.connection_manager_.config_.routeConfig().redirectRequest(headers,
                                                                               parent_.stream_id_);
    }
    const Router::RouteEntry* routeForRequest(const HeaderMap& headers) const {
      return parent_.connection_manager_.config_.routeConfig().routeForRequest(headers,
                                                                               parent_.stream_id_);
    }

    ActiveStream& parent_;
    bool headers_continued_{};
    bool stopped_{};
  };

  /**
   * Wrapper for a stream decoder filter.
   */
  struct ActiveStreamDecoderFilter : public ActiveStreamFilterBase,
                                     public StreamDecoderFilterCallbacks,
                                     LinkedObject<ActiveStreamDecoderFilter> {
    ActiveStreamDecoderFilter(ActiveStream& parent, StreamDecoderFilterPtr filter)
        : ActiveStreamFilterBase(parent), handle_(filter) {}

    // ActiveStreamFilterBase
    Buffer::InstancePtr& bufferedData() override { return parent_.buffered_request_data_; }
    bool complete() override { return parent_.state_.remote_complete_; }
    void doHeaders(bool end_stream) override {
      parent_.decodeHeaders(this, *parent_.request_headers_, end_stream);
    }
    void doData(bool end_stream) override {
      parent_.decodeData(this, *parent_.buffered_request_data_, end_stream);
    }
    void doTrailers() override { parent_.decodeTrailers(this, *parent_.request_trailers_); }
    const HeaderMapPtr& trailers() override { return parent_.request_trailers_; }

    // Http::StreamDecoderFilterCallbacks
    void continueDecoding() override;
    const Buffer::Instance* decodingBuffer() override {
      return parent_.buffered_request_data_.get();
    }
    void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(HeaderMapPtr&& trailers) override;

    StreamDecoderFilterPtr handle_;
  };

  typedef std::unique_ptr<ActiveStreamDecoderFilter> ActiveStreamDecoderFilterPtr;

  /**
   * Wrapper for a stream encoder filter.
   */
  struct ActiveStreamEncoderFilter : public ActiveStreamFilterBase,
                                     public StreamEncoderFilterCallbacks,
                                     LinkedObject<ActiveStreamEncoderFilter> {
    ActiveStreamEncoderFilter(ActiveStream& parent, StreamEncoderFilterPtr filter)
        : ActiveStreamFilterBase(parent), handle_(filter) {}

    // ActiveStreamFilterBase
    Buffer::InstancePtr& bufferedData() override { return parent_.buffered_response_data_; }
    bool complete() override { return parent_.state_.local_complete_; }
    void doHeaders(bool end_stream) override {
      parent_.encodeHeaders(this, *parent_.response_headers_, end_stream);
    }
    void doData(bool end_stream) override {
      parent_.encodeData(this, *parent_.buffered_response_data_, end_stream);
    }
    void doTrailers() override { parent_.encodeTrailers(this, *parent_.response_trailers_); }
    const HeaderMapPtr& trailers() override { return parent_.response_trailers_; }

    // Http::StreamEncoderFilterCallbacks
    void continueEncoding() override;
    const Buffer::Instance* encodingBuffer() override {
      return parent_.buffered_response_data_.get();
    }

    StreamEncoderFilterPtr handle_;
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
                        public Tracing::TracingContext {
    ActiveStream(ConnectionManagerImpl& connection_manager);
    ~ActiveStream();

    void chargeStats(HeaderMap& headers);
    std::list<ActiveStreamEncoderFilterPtr>::iterator
    commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream);
    uint64_t connectionId();
    void decodeHeaders(ActiveStreamDecoderFilter* filter, HeaderMap& headers, bool end_stream);
    void decodeData(ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream);
    void decodeTrailers(ActiveStreamDecoderFilter* filter, HeaderMap& trailers);
    void encodeHeaders(ActiveStreamEncoderFilter* filter, HeaderMap& headers, bool end_stream);
    void encodeData(ActiveStreamEncoderFilter* filter, Buffer::Instance& data, bool end_stream);
    void encodeTrailers(ActiveStreamEncoderFilter* filter, HeaderMap& trailers);
    void maybeEndEncode(bool end_stream);
    uint64_t streamId() { return stream_id_; }

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason reason) override;

    // Http::StreamDecoder
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(HeaderMapPtr&& trailers) override;

    // Http::FilterChainFactoryCallbacks
    void addStreamDecoderFilter(StreamDecoderFilterPtr filter) override;
    void addStreamEncoderFilter(StreamEncoderFilterPtr filter) override;
    void addStreamFilter(StreamFilterPtr filter) override;

    // Tracing::TracingContext
    virtual const std::string& operationName() const override;

    static DateFormatter date_formatter_;

    // All state for the stream. Put here for readability. We could move this to a bit field
    // eventually if we want.
    struct State {
      bool remote_complete_{};
      bool local_complete_{};
    };

    // NOTE: This is used for stable randomness. For performance reasons we use an incrementing
    //       counter shared across all threads. This may lead to burstiness but in general should
    //       prove the intended behavior when doing runtime routing, etc.
    static std::atomic<uint64_t> next_stream_id_;

    ConnectionManagerImpl& connection_manager_;
    const uint64_t stream_id_;
    StreamEncoder* response_encoder_{};
    HeaderMapPtr response_headers_;
    Buffer::InstancePtr buffered_response_data_; // TODO: buffer data stat
    HeaderMapPtr response_trailers_{};
    HeaderMapPtr request_headers_;
    Buffer::InstancePtr buffered_request_data_; // TODO: buffer data stat
    HeaderMapPtr request_trailers_;
    std::list<ActiveStreamDecoderFilterPtr> decoder_filters_;
    std::list<ActiveStreamEncoderFilterPtr> encoder_filters_;
    Stats::TimespanPtr request_timer_;
    std::list<std::function<void()>> reset_callbacks_;
    State state_;
    AccessLog::RequestInfoImpl request_info_;
    std::string downstream_address_;
  };

  typedef std::unique_ptr<ActiveStream> ActiveStreamPtr;

  /**
   * Check to see if the connection can be closed after gracefully waiting to send pending codec
   * data.
   */
  void checkForDeferredClose();

  /**
   * Do a delayed destruction of a stream to allow for stack unwind.
   */
  void destroyStream(ActiveStream& stream);

  void resetAllStreams();
  void onIdleTimeout();
  void onDrainTimeout();
  void startDrainSequence();

  enum class DrainState { NotDraining, Draining, Closing };

  ConnectionManagerConfig& config_;
  ServerConnectionPtr codec_;
  std::list<ActiveStreamPtr> streams_;
  Stats::TimespanPtr conn_length_;
  Network::DrainDecision& drain_close_;
  DrainState drain_state_{DrainState::NotDraining};
  UserAgent user_agent_;
  Event::TimerPtr idle_timer_;
  Event::TimerPtr drain_timer_;
  Runtime::RandomGenerator& random_generator_;
  Tracing::HttpTracer& tracer_;
  Runtime::Loader& runtime_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // Http
