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
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/router/rds.h"
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

  /**
   * Base class wrapper for both stream encoder and decoder filters.
   */
  struct ActiveStreamFilterBase : public virtual StreamFilterCallbacks {
    ActiveStreamFilterBase(ActiveStream& parent, bool dual_filter)
        : parent_(parent), iteration_state_(IterationState::Continue),
          iterate_from_current_filter_(false), headers_continued_(false),
          continue_headers_continued_(false), end_stream_(false), dual_filter_(dual_filter),
          decode_headers_called_(false), encode_headers_called_(false) {}

    // Functions in the following block are called after the filter finishes processing
    // corresponding data. Those functions handle state updates and data storage (if needed)
    // according to the status returned by filter's callback functions.
    bool commonHandleAfter100ContinueHeadersCallback(FilterHeadersStatus status);
    bool commonHandleAfterHeadersCallback(FilterHeadersStatus status, bool& headers_only);
    bool commonHandleAfterDataCallback(FilterDataStatus status, Buffer::Instance& provided_data,
                                       bool& buffer_was_streaming);
    bool commonHandleAfterTrailersCallback(FilterTrailersStatus status);

    // Buffers provided_data.
    void commonHandleBufferData(Buffer::Instance& provided_data);

    // If iteration has stopped for all frame types, calls this function to buffer the data before
    // the filter processes data. The function also updates streaming state.
    void commonBufferDataIfStopAll(Buffer::Instance& provided_data, bool& buffer_was_streaming);

    void commonContinue();
    virtual bool canContinue() PURE;
    virtual Buffer::WatermarkBufferPtr createBuffer() PURE;
    virtual Buffer::WatermarkBufferPtr& bufferedData() PURE;
    virtual bool complete() PURE;
    virtual bool has100Continueheaders() PURE;
    virtual void do100ContinueHeaders() PURE;
    virtual void doHeaders(bool end_stream) PURE;
    virtual void doData(bool end_stream) PURE;
    virtual void doTrailers() PURE;
    virtual bool hasTrailers() PURE;
    virtual void doMetadata() PURE;
    // TODO(soya3129): make this pure when adding impl to encoder filter.
    virtual void handleMetadataAfterHeadersCallback() PURE;

    // Http::StreamFilterCallbacks
    const Network::Connection* connection() override;
    Event::Dispatcher& dispatcher() override;
    void resetStream() override;
    Router::RouteConstSharedPtr route() override;
    Router::RouteConstSharedPtr route(const Router::RouteCallback& cb) override;
    Upstream::ClusterInfoConstSharedPtr clusterInfo() override;
    void clearRouteCache() override;
    uint64_t streamId() const override;
    StreamInfo::StreamInfo& streamInfo() override;
    Tracing::Span& activeSpan() override;
    Tracing::Config& tracingConfig() override;
    const ScopeTrackedObject& scope() override { return parent_; }

    // Functions to set or get iteration state.
    bool canIterate() { return iteration_state_ == IterationState::Continue; }
    bool stoppedAll() {
      return iteration_state_ == IterationState::StopAllBuffer ||
             iteration_state_ == IterationState::StopAllWatermark;
    }
    void allowIteration() {
      ASSERT(iteration_state_ != IterationState::Continue);
      iteration_state_ = IterationState::Continue;
    }
    MetadataMapVector* getSavedRequestMetadata() {
      if (saved_request_metadata_ == nullptr) {
        saved_request_metadata_ = std::make_unique<MetadataMapVector>();
      }
      return saved_request_metadata_.get();
    }
    MetadataMapVector* getSavedResponseMetadata() {
      if (saved_response_metadata_ == nullptr) {
        saved_response_metadata_ = std::make_unique<MetadataMapVector>();
      }
      return saved_response_metadata_.get();
    }

    // A vector to save metadata when the current filter's [de|en]codeMetadata() can not be called,
    // either because [de|en]codeHeaders() of the current filter returns StopAllIteration or because
    // [de|en]codeHeaders() adds new metadata to [de|en]code, but we don't know
    // [de|en]codeHeaders()'s return value yet. The storage is created on demand.
    std::unique_ptr<MetadataMapVector> saved_request_metadata_{nullptr};
    std::unique_ptr<MetadataMapVector> saved_response_metadata_{nullptr};
    // The state of iteration.
    enum class IterationState {
      Continue,            // Iteration has not stopped for any frame type.
      StopSingleIteration, // Iteration has stopped for headers, 100-continue, or data.
      StopAllBuffer,       // Iteration has stopped for all frame types, and following data should
                           // be buffered.
      StopAllWatermark,    // Iteration has stopped for all frame types, and following data should
                           // be buffered until high watermark is reached.
    };
    ActiveStream& parent_;
    IterationState iteration_state_;
    // If the filter resumes iteration from a StopAllBuffer/Watermark state, the current filter
    // hasn't parsed data and trailers. As a result, the filter iteration should start with the
    // current filter instead of the next one. If true, filter iteration starts with the current
    // filter. Otherwise, starts with the next filter in the chain.
    bool iterate_from_current_filter_ : 1;
    bool headers_continued_ : 1;
    bool continue_headers_continued_ : 1;
    // If true, end_stream is called for this filter.
    bool end_stream_ : 1;
    const bool dual_filter_ : 1;
    bool decode_headers_called_ : 1;
    bool encode_headers_called_ : 1;
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
    bool has100Continueheaders() override { return false; }
    void do100ContinueHeaders() override { NOT_REACHED_GCOVR_EXCL_LINE; }
    void doHeaders(bool end_stream) override {
      parent_.decodeHeaders(this, *parent_.request_headers_, end_stream);
    }
    void doData(bool end_stream) override {
      parent_.decodeData(this, *parent_.buffered_request_data_, end_stream,
                         ActiveStream::FilterIterationStartState::CanStartFromCurrent);
    }
    void doMetadata() override {
      if (saved_request_metadata_ != nullptr) {
        drainSavedRequestMetadata();
      }
    }
    void doTrailers() override { parent_.decodeTrailers(this, *parent_.request_trailers_); }
    bool hasTrailers() override { return parent_.request_trailers_ != nullptr; }

    void drainSavedRequestMetadata() {
      ASSERT(saved_request_metadata_ != nullptr);
      for (auto& metadata_map : *getSavedRequestMetadata()) {
        parent_.decodeMetadata(this, *metadata_map);
      }
      getSavedRequestMetadata()->clear();
    }
    // This function is called after the filter calls decodeHeaders() to drain accumulated metadata.
    void handleMetadataAfterHeadersCallback() override;

    // Http::StreamDecoderFilterCallbacks
    void addDecodedData(Buffer::Instance& data, bool streaming) override;
    void injectDecodedDataToFilterChain(Buffer::Instance& data, bool end_stream) override;
    RequestTrailerMap& addDecodedTrailers() override;
    MetadataMapVector& addDecodedMetadata() override;
    void continueDecoding() override;
    const Buffer::Instance* decodingBuffer() override {
      return parent_.buffered_request_data_.get();
    }

    void modifyDecodingBuffer(std::function<void(Buffer::Instance&)> callback) override {
      ASSERT(parent_.state_.latest_data_decoding_filter_ == this);
      callback(*parent_.buffered_request_data_.get());
    }

    void sendLocalReply(Code code, absl::string_view body,
                        std::function<void(ResponseHeaderMap& headers)> modify_headers,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                        absl::string_view details) override {
      parent_.stream_info_.setResponseCodeDetails(details);
      parent_.sendLocalReply(is_grpc_request_, code, body, modify_headers,
                             parent_.state_.is_head_request_, grpc_status, details);
    }
    void encode100ContinueHeaders(ResponseHeaderMapPtr&& headers) override;
    void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void encodeData(Buffer::Instance& data, bool end_stream) override;
    void encodeTrailers(ResponseTrailerMapPtr&& trailers) override;
    void encodeMetadata(MetadataMapPtr&& metadata_map_ptr) override;
    void onDecoderFilterAboveWriteBufferHighWatermark() override;
    void onDecoderFilterBelowWriteBufferLowWatermark() override;
    void
    addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& watermark_callbacks) override;
    void
    removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& watermark_callbacks) override;
    void setDecoderBufferLimit(uint32_t limit) override { parent_.setBufferLimit(limit); }
    uint32_t decoderBufferLimit() override { return parent_.buffer_limit_; }
    bool recreateStream() override;

    void addUpstreamSocketOptions(const Network::Socket::OptionsSharedPtr& options) override {
      Network::Socket::appendOptions(parent_.upstream_options_, options);
    }

    Network::Socket::OptionsSharedPtr getUpstreamSocketOptions() const override {
      return parent_.upstream_options_;
    }

    // Each decoder filter instance checks if the request passed to the filter is gRPC
    // so that we can issue gRPC local responses to gRPC requests. Filter's decodeHeaders()
    // called here may change the content type, so we must check it before the call.
    FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
      is_grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);
      FilterHeadersStatus status = handle_->decodeHeaders(headers, end_stream);
      if (end_stream) {
        handle_->decodeComplete();
      }
      return status;
    }

    void requestDataTooLarge();
    void requestDataDrained();

    void requestRouteConfigUpdate(
        Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) override;
    absl::optional<Router::ConfigConstSharedPtr> routeConfig() override;

    StreamDecoderFilterSharedPtr handle_;
    bool is_grpc_request_{};
  };

  using ActiveStreamDecoderFilterPtr = std::unique_ptr<ActiveStreamDecoderFilter>;

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
    bool has100Continueheaders() override {
      return parent_.state_.has_continue_headers_ && !continue_headers_continued_;
    }
    void do100ContinueHeaders() override {
      parent_.encode100ContinueHeaders(this, *parent_.continue_headers_);
    }
    void doHeaders(bool end_stream) override {
      parent_.encodeHeaders(this, *parent_.response_headers_, end_stream);
    }
    void doData(bool end_stream) override {
      parent_.encodeData(this, *parent_.buffered_response_data_, end_stream,
                         ActiveStream::FilterIterationStartState::CanStartFromCurrent);
    }
    void drainSavedResponseMetadata() {
      ASSERT(saved_response_metadata_ != nullptr);
      for (auto& metadata_map : *getSavedResponseMetadata()) {
        parent_.encodeMetadata(this, std::move(metadata_map));
      }
      getSavedResponseMetadata()->clear();
    }
    void handleMetadataAfterHeadersCallback() override;

    void doMetadata() override {
      if (saved_response_metadata_ != nullptr) {
        drainSavedResponseMetadata();
      }
    }
    void doTrailers() override { parent_.encodeTrailers(this, *parent_.response_trailers_); }
    bool hasTrailers() override { return parent_.response_trailers_ != nullptr; }

    // Http::StreamEncoderFilterCallbacks
    void addEncodedData(Buffer::Instance& data, bool streaming) override;
    void injectEncodedDataToFilterChain(Buffer::Instance& data, bool end_stream) override;
    ResponseTrailerMap& addEncodedTrailers() override;
    void addEncodedMetadata(MetadataMapPtr&& metadata_map) override;
    void onEncoderFilterAboveWriteBufferHighWatermark() override;
    void onEncoderFilterBelowWriteBufferLowWatermark() override;
    void setEncoderBufferLimit(uint32_t limit) override { parent_.setBufferLimit(limit); }
    uint32_t encoderBufferLimit() override { return parent_.buffer_limit_; }
    void continueEncoding() override;
    const Buffer::Instance* encodingBuffer() override {
      return parent_.buffered_response_data_.get();
    }
    void modifyEncodingBuffer(std::function<void(Buffer::Instance&)> callback) override {
      ASSERT(parent_.state_.latest_data_encoding_filter_ == this);
      callback(*parent_.buffered_response_data_.get());
    }
    Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override {
      // TODO(mattklein123): At some point we might want to actually wrap this interface but for now
      // we give the filter direct access to the encoder options.
      return parent_.response_encoder_->http1StreamEncoderOptions();
    }

    void responseDataTooLarge();
    void responseDataDrained();

    StreamEncoderFilterSharedPtr handle_;
  };

  using ActiveStreamEncoderFilterPtr = std::unique_ptr<ActiveStreamEncoderFilter>;

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
                        public FilterChainFactoryCallbacks,
                        public Tracing::Config,
                        public ScopeTrackedObject {
    ActiveStream(ConnectionManagerImpl& connection_manager);
    ~ActiveStream() override;

    // Indicates which filter to start the iteration with.
    enum class FilterIterationStartState { AlwaysStartFromNext, CanStartFromCurrent };

    void addStreamDecoderFilterWorker(StreamDecoderFilterSharedPtr filter, bool dual_filter);
    void addStreamEncoderFilterWorker(StreamEncoderFilterSharedPtr filter, bool dual_filter);
    void chargeStats(const ResponseHeaderMap& headers);
    // Returns the encoder filter to start iteration with.
    std::list<ActiveStreamEncoderFilterPtr>::iterator
    commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream,
                       FilterIterationStartState filter_iteration_start_state);
    // Returns the decoder filter to start iteration with.
    std::list<ActiveStreamDecoderFilterPtr>::iterator
    commonDecodePrefix(ActiveStreamDecoderFilter* filter,
                       FilterIterationStartState filter_iteration_start_state);
    const Network::Connection* connection();
    void addDecodedData(ActiveStreamDecoderFilter& filter, Buffer::Instance& data, bool streaming);
    RequestTrailerMap& addDecodedTrailers();
    MetadataMapVector& addDecodedMetadata();
    // Helper function for the case where we have a header only request, but a filter adds a body
    // to it.
    void maybeContinueDecoding(
        const std::list<ActiveStreamDecoderFilterPtr>::iterator& maybe_continue_data_entry);
    void decodeHeaders(ActiveStreamDecoderFilter* filter, RequestHeaderMap& headers,
                       bool end_stream);
    // Sends data through decoding filter chains. filter_iteration_start_state indicates which
    // filter to start the iteration with.
    void decodeData(ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream,
                    FilterIterationStartState filter_iteration_start_state);
    void decodeTrailers(ActiveStreamDecoderFilter* filter, RequestTrailerMap& trailers);
    void decodeMetadata(ActiveStreamDecoderFilter* filter, MetadataMap& metadata_map);
    void disarmRequestTimeout();
    void maybeEndDecode(bool end_stream);
    void addEncodedData(ActiveStreamEncoderFilter& filter, Buffer::Instance& data, bool streaming);
    ResponseTrailerMap& addEncodedTrailers();
    void sendLocalReply(bool is_grpc_request, Code code, absl::string_view body,
                        const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
                        bool is_head_request,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                        absl::string_view details) override;
    void sendLocalReplyViaFilterChain(
        bool is_grpc_request, Code code, absl::string_view body,
        const std::function<void(ResponseHeaderMap& headers)>& modify_headers, bool is_head_request,
        const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details);
    void encode100ContinueHeaders(ActiveStreamEncoderFilter* filter, ResponseHeaderMap& headers);
    // As with most of the encode functions, this runs encodeHeaders on various
    // filters before calling encodeHeadersInternal which does final header munging and passes the
    // headers to the encoder.
    void maybeContinueEncoding(
        const std::list<ActiveStreamEncoderFilterPtr>::iterator& maybe_continue_data_entry);
    void encodeHeaders(ActiveStreamEncoderFilter* filter, ResponseHeaderMap& headers,
                       bool end_stream);
    // Sends data through encoding filter chains. filter_iteration_start_state indicates which
    // filter to start the iteration with, and finally calls encodeDataInternal
    // to update stats, do end stream bookkeeping, and send the data to encoder.
    void encodeData(ActiveStreamEncoderFilter* filter, Buffer::Instance& data, bool end_stream,
                    FilterIterationStartState filter_iteration_start_state);
    void encodeTrailers(ActiveStreamEncoderFilter* filter, ResponseTrailerMap& trailers);
    void encodeMetadata(ActiveStreamEncoderFilter* filter, MetadataMapPtr&& metadata_map_ptr);

    // This is a helper function for encodeHeaders and responseDataTooLarge which allows for shared
    // code for the two headers encoding paths. It does header munging, updates timing stats, and
    // sends the headers to the encoder.
    void encodeHeadersInternal(ResponseHeaderMap& headers, bool end_stream);
    // This is a helper function for encodeData and responseDataTooLarge which allows for shared
    // code for the two data encoding paths. It does stats updates and tracks potential end of
    // stream.
    void encodeDataInternal(Buffer::Instance& data, bool end_stream);

    void maybeEndEncode(bool end_stream);
    // Returns true if new metadata is decoded. Otherwise, returns false.
    bool processNewlyAddedMetadata();
    uint64_t streamId() { return stream_id_; }
    // Returns true if filter has stopped iteration for all frame types. Otherwise, returns false.
    // filter_streaming is the variable to indicate if stream is streaming, and its value may be
    // changed by the function.
    bool handleDataIfStopAll(ActiveStreamFilterBase& filter, Buffer::Instance& data,
                             bool& filter_streaming);

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
    Tracing::OperationName operationName() const override;
    const Tracing::CustomTagMap* customTags() const override;
    bool verbose() const override;
    uint32_t maxPathTagLength() const override;

    // ScopeTrackedObject
    void dumpState(std::ostream& os, int indent_level = 0) const override {
      const char* spaces = spacesForLevel(indent_level);
      os << spaces << "ActiveStream " << this << DUMP_MEMBER(stream_id_)
         << DUMP_MEMBER(state_.has_continue_headers_) << DUMP_MEMBER(state_.is_head_request_)
         << DUMP_MEMBER(state_.decoding_headers_only_) << DUMP_MEMBER(state_.encoding_headers_only_)
         << "\n";

      DUMP_DETAILS(request_headers_);
      DUMP_DETAILS(request_trailers_);
      DUMP_DETAILS(response_headers_);
      DUMP_DETAILS(response_trailers_);
      DUMP_DETAILS(&stream_info_);
    }

    void traceRequest();

    // Updates the snapped_route_config_ (by reselecting scoped route configuration), if a scope is
    // not found, snapped_route_config_ is set to Router::NullConfigImpl.
    void snapScopedRouteConfig();

    void refreshCachedRoute();
    void refreshCachedRoute(const Router::RouteCallback& cb);
    void
    requestRouteConfigUpdate(Event::Dispatcher& thread_local_dispatcher,
                             Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb);
    absl::optional<Router::ConfigConstSharedPtr> routeConfig();

    void refreshCachedTracingCustomTags();

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
          : remote_complete_(false), local_complete_(false), codec_saw_local_complete_(false),
            saw_connection_close_(false), successful_upgrade_(false), created_filter_chain_(false),
            is_internally_created_(false), decorated_propagate_(true), has_continue_headers_(false),
            is_head_request_(false), non_100_response_headers_encoded_(false) {}

      uint32_t filter_call_state_{0};
      // The following 3 members are booleans rather than part of the space-saving bitfield as they
      // are passed as arguments to functions expecting bools. Extend State using the bitfield
      // where possible.
      bool encoder_filters_streaming_{true};
      bool decoder_filters_streaming_{true};
      bool destroyed_{false};
      bool remote_complete_ : 1;
      bool local_complete_ : 1; // This indicates that local is complete prior to filter processing.
                                // A filter can still stop the stream from being complete as seen
                                // by the codec.
      bool codec_saw_local_complete_ : 1; // This indicates that local is complete as written all
                                          // the way through to the codec.
      bool saw_connection_close_ : 1;
      bool successful_upgrade_ : 1;
      bool created_filter_chain_ : 1;

      // True if this stream is internally created. Currently only used for
      // internal redirects or other streams created via recreateStream().
      bool is_internally_created_ : 1;

      bool decorated_propagate_ : 1;
      // By default, we will assume there are no 100-Continue headers. If encode100ContinueHeaders
      // is ever called, this is set to true so commonContinue resumes processing the 100-Continue.
      bool has_continue_headers_ : 1;
      bool is_head_request_ : 1;
      // Tracks if headers other than 100-Continue have been encoded to the codec.
      bool non_100_response_headers_encoded_ : 1;
      // Whether a filter has indicated that the request should be treated as a headers only
      // request.
      bool decoding_headers_only_{false};
      // Whether a filter has indicated that the response should be treated as a headers only
      // response.
      bool encoding_headers_only_{false};

      // Used to track which filter is the latest filter that has received data.
      ActiveStreamEncoderFilter* latest_data_encoding_filter_{};
      ActiveStreamDecoderFilter* latest_data_decoding_filter_{};
    };

    // Possibly increases buffer_limit_ to the value of limit.
    void setBufferLimit(uint32_t limit);
    // Set up the Encoder/Decoder filter chain.
    bool createFilterChain();
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
      s.dumpState(os);
      return os;
    }

    MetadataMapVector* getRequestMetadataMapVector() {
      if (request_metadata_map_vector_ == nullptr) {
        request_metadata_map_vector_ = std::make_unique<MetadataMapVector>();
      }
      return request_metadata_map_vector_.get();
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
    Tracing::SpanPtr active_span_;
    const uint64_t stream_id_;
    ResponseEncoder* response_encoder_{};
    ResponseHeaderMapPtr continue_headers_;
    ResponseHeaderMapPtr response_headers_;
    Buffer::WatermarkBufferPtr buffered_response_data_;
    ResponseTrailerMapPtr response_trailers_{};
    RequestHeaderMapPtr request_headers_;
    Buffer::WatermarkBufferPtr buffered_request_data_;
    RequestTrailerMapPtr request_trailers_;
    std::list<ActiveStreamDecoderFilterPtr> decoder_filters_;
    std::list<ActiveStreamEncoderFilterPtr> encoder_filters_;
    std::list<AccessLog::InstanceSharedPtr> access_log_handlers_;
    Stats::TimespanPtr request_response_timespan_;
    // Per-stream idle timeout.
    Event::TimerPtr stream_idle_timer_;
    // Per-stream request timeout.
    Event::TimerPtr request_timer_;
    // Per-stream alive duration.
    Event::TimerPtr max_stream_duration_timer_;
    std::chrono::milliseconds idle_timeout_ms_{};
    State state_;
    StreamInfo::StreamInfoImpl stream_info_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;
    absl::optional<Upstream::ClusterInfoConstSharedPtr> cached_cluster_info_;
    std::list<DownstreamWatermarkCallbacks*> watermark_callbacks_{};
    // Stores metadata added in the decoding filter that is being processed. Will be cleared before
    // processing the next filter. The storage is created on demand. We need to store metadata
    // temporarily in the filter in case the filter has stopped all while processing headers.
    std::unique_ptr<MetadataMapVector> request_metadata_map_vector_{nullptr};
    uint32_t buffer_limit_{0};
    uint32_t high_watermark_count_{0};
    const std::string* decorated_operation_{nullptr};
    Network::Socket::OptionsSharedPtr upstream_options_;
    std::unique_ptr<RouteConfigUpdateRequester> route_config_update_requester_;
    std::unique_ptr<Tracing::CustomTagMap> tracing_custom_tags_{nullptr};
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
  Http::Context& http_context_;
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
