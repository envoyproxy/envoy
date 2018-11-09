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
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/router/rds.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/overload_manager.h"
#include "envoy/ssl/connection.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/upstream.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/linked_object.h"
#include "common/grpc/common.h"
#include "common/http/conn_manager_config.h"
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
                              public Network::ConnectionCallbacks {
public:
  ConnectionManagerImpl(ConnectionManagerConfig& config, const Network::DrainDecision& drain_close,
                        Runtime::RandomGenerator& random_generator, Tracing::HttpTracer& tracer,
                        Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
                        Upstream::ClusterManager& cluster_manager,
                        Server::OverloadManager* overload_manager, Event::TimeSystem& time_system);
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

  Event::TimeSystem& timeSystem() { return time_system_; }

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
    Upstream::ClusterInfoConstSharedPtr clusterInfo() override;
    void clearRouteCache() override;
    uint64_t streamId() override;
    StreamInfo::StreamInfo& streamInfo() override;
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
    void do100ContinueHeaders() override { NOT_REACHED_GCOVR_EXCL_LINE; }
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
    HeaderMap& addDecodedTrailers() override;
    void continueDecoding() override;
    const Buffer::Instance* decodingBuffer() override {
      return parent_.buffered_request_data_.get();
    }
    void sendLocalReply(Code code, const std::string& body,
                        std::function<void(HeaderMap& headers)> modify_headers,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status) override {
      parent_.sendLocalReply(is_grpc_request_, code, body, modify_headers, parent_.is_head_request_,
                             grpc_status);
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

    // Each decoder filter instance checks if the request passed to the filter is gRPC
    // so that we can issue gRPC local responses to gRPC requests. Filter's decodeHeaders()
    // called here may change the content type, so we must check it before the call.
    FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) {
      is_grpc_request_ = Grpc::Common::hasGrpcContentType(headers);
      return handle_->decodeHeaders(headers, end_stream);
    }

    void requestDataTooLarge();
    void requestDataDrained();

    StreamDecoderFilterSharedPtr handle_;
    bool is_grpc_request_{};
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
    HeaderMap& addEncodedTrailers() override;
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
                        public Tracing::Config {
    ActiveStream(ConnectionManagerImpl& connection_manager);
    ~ActiveStream();

    void addStreamDecoderFilterWorker(StreamDecoderFilterSharedPtr filter, bool dual_filter);
    void addStreamEncoderFilterWorker(StreamEncoderFilterSharedPtr filter, bool dual_filter);
    void chargeStats(const HeaderMap& headers);
    std::list<ActiveStreamEncoderFilterPtr>::iterator
    commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream);
    const Network::Connection* connection();
    void addDecodedData(ActiveStreamDecoderFilter& filter, Buffer::Instance& data, bool streaming);
    HeaderMap& addDecodedTrailers();
    void decodeHeaders(ActiveStreamDecoderFilter* filter, HeaderMap& headers, bool end_stream);
    void decodeData(ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream);
    void decodeTrailers(ActiveStreamDecoderFilter* filter, HeaderMap& trailers);
    void disarmRequestTimeout();
    void maybeEndDecode(bool end_stream);
    void addEncodedData(ActiveStreamEncoderFilter& filter, Buffer::Instance& data, bool streaming);
    HeaderMap& addEncodedTrailers();
    void sendLocalReply(bool is_grpc_request, Code code, const std::string& body,
                        const std::function<void(HeaderMap& headers)>& modify_headers,
                        bool is_head_request,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status);
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
    void decode100ContinueHeaders(HeaderMapPtr&&) override { NOT_REACHED_GCOVR_EXCL_LINE; }
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(HeaderMapPtr&& trailers) override;
    void decodeMetadata(MetadataMapPtr&&) override { NOT_REACHED_GCOVR_EXCL_LINE; }

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
      // Used to indicate that we're processing the final [En|De]codeData frame,
      // i.e. end_stream = true
      static constexpr uint32_t LastDataFrame = 0x80;
    };
    // clang-format on

    // All state for the stream. Put here for readability.
    struct State {
      State()
          : remote_complete_(false), local_complete_(false), saw_connection_close_(false),
            successful_upgrade_(false), created_filter_chain_(false) {}

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
      bool successful_upgrade_ : 1;
      bool created_filter_chain_ : 1;
    };

    // Possibly increases buffer_limit_ to the value of limit.
    void setBufferLimit(uint32_t limit);
    // Set up the Encoder/Decoder filter chain.
    bool createFilterChain();
    // Per-stream idle timeout callback.
    void onIdleTimeout();
    // Reset per-stream idle timer.
    void resetIdleTimer();
    // Per-stream request timeout callback
    void onRequestTimeout();

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
    Stats::TimespanPtr request_response_timespan_;
    // Per-stream idle timeout.
    Event::TimerPtr stream_idle_timer_;
    // Per-stream request timeout.
    Event::TimerPtr request_timer_;
    std::chrono::milliseconds idle_timeout_ms_{};
    State state_;
    StreamInfo::StreamInfoImpl stream_info_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    absl::optional<Upstream::ClusterInfoConstSharedPtr> cached_cluster_info_;
    DownstreamWatermarkCallbacks* watermark_callbacks_{nullptr};
    uint32_t buffer_limit_{0};
    uint32_t high_watermark_count_{0};
    const std::string* decorated_operation_{nullptr};
    // By default, we will assume there are no 100-Continue headers. If encode100ContinueHeaders
    // is ever called, this is set to true so commonContinue resumes processing the 100-Continue.
    bool has_continue_headers_{};
    bool is_head_request_{false};
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
  Event::TimerPtr drain_timer_;
  Runtime::RandomGenerator& random_generator_;
  Tracing::HttpTracer& tracer_;
  Runtime::Loader& runtime_;
  const LocalInfo::LocalInfo& local_info_;
  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  ConnectionManagerListenerStats& listener_stats_;
  // References into the overload manager thread local state map. Using these lets us avoid a map
  // lookup in the hot path of processing each request.
  const Server::OverloadActionState& overload_stop_accepting_requests_ref_;
  const Server::OverloadActionState& overload_disable_keepalive_ref_;
  Event::TimeSystem& time_system_;
};

} // namespace Http
} // namespace Envoy
