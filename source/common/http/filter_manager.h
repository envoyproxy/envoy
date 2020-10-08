#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/dump_state_utils.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/local_reply/local_reply.h"

namespace Envoy {
namespace Http {

class FilterManager;

/**
 * Base class wrapper for both stream encoder and decoder filters.
 */
struct ActiveStreamFilterBase : public virtual StreamFilterCallbacks,
                                Logger::Loggable<Logger::Id::http> {
  ActiveStreamFilterBase(FilterManager& parent, bool dual_filter)
      : parent_(parent), iteration_state_(IterationState::Continue),
        iterate_from_current_filter_(false), headers_continued_(false),
        continue_headers_continued_(false), end_stream_(false), dual_filter_(dual_filter),
        decode_headers_called_(false), encode_headers_called_(false) {}

  // Functions in the following block are called after the filter finishes processing
  // corresponding data. Those functions handle state updates and data storage (if needed)
  // according to the status returned by filter's callback functions.
  bool commonHandleAfter100ContinueHeadersCallback(FilterHeadersStatus status);
  bool commonHandleAfterHeadersCallback(FilterHeadersStatus status, bool& end_stream);
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
  const ScopeTrackedObject& scope() override;

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
  FilterManager& parent_;
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
  ActiveStreamDecoderFilter(FilterManager& parent, StreamDecoderFilterSharedPtr filter,
                            bool dual_filter)
      : ActiveStreamFilterBase(parent, dual_filter), handle_(filter) {}

  // ActiveStreamFilterBase
  bool canContinue() override;
  Buffer::WatermarkBufferPtr createBuffer() override;
  Buffer::WatermarkBufferPtr& bufferedData() override;
  bool complete() override;
  bool has100Continueheaders() override { return false; }
  void do100ContinueHeaders() override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void doHeaders(bool end_stream) override;
  void doData(bool end_stream) override;
  void doMetadata() override {
    if (saved_request_metadata_ != nullptr) {
      drainSavedRequestMetadata();
    }
  }
  void doTrailers() override;
  bool hasTrailers() override;

  void drainSavedRequestMetadata();
  // This function is called after the filter calls decodeHeaders() to drain accumulated metadata.
  void handleMetadataAfterHeadersCallback() override;

  // Http::StreamDecoderFilterCallbacks
  void addDecodedData(Buffer::Instance& data, bool streaming) override;
  void injectDecodedDataToFilterChain(Buffer::Instance& data, bool end_stream) override;
  RequestTrailerMap& addDecodedTrailers() override;
  MetadataMapVector& addDecodedMetadata() override;
  void continueDecoding() override;
  const Buffer::Instance* decodingBuffer() override;

  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)> callback) override;

  void sendLocalReply(Code code, absl::string_view body,
                      std::function<void(ResponseHeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details) override;
  void encode100ContinueHeaders(ResponseHeaderMapPtr&& headers) override;
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                     absl::string_view details) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override;
  void encodeMetadata(MetadataMapPtr&& metadata_map_ptr) override;
  void onDecoderFilterAboveWriteBufferHighWatermark() override;
  void onDecoderFilterBelowWriteBufferLowWatermark() override;
  void addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& watermark_callbacks) override;
  void
  removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& watermark_callbacks) override;
  void setDecoderBufferLimit(uint32_t limit) override;
  uint32_t decoderBufferLimit() override;
  bool recreateStream() override;

  void addUpstreamSocketOptions(const Network::Socket::OptionsSharedPtr& options) override;

  Network::Socket::OptionsSharedPtr getUpstreamSocketOptions() const override;

  // Each decoder filter instance checks if the request passed to the filter is gRPC
  // so that we can issue gRPC local responses to gRPC requests. Filter's decodeHeaders()
  // called here may change the content type, so we must check it before the call.
  FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    is_grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);
    FilterHeadersStatus status = handle_->decodeHeaders(headers, end_stream);
    return status;
  }

  void requestDataTooLarge();
  void requestDataDrained();

  void requestRouteConfigUpdate(
      Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) override;
  absl::optional<Router::ConfigConstSharedPtr> routeConfig();

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
  ActiveStreamEncoderFilter(FilterManager& parent, StreamEncoderFilterSharedPtr filter,
                            bool dual_filter)
      : ActiveStreamFilterBase(parent, dual_filter), handle_(filter) {}

  // ActiveStreamFilterBase
  bool canContinue() override { return true; }
  Buffer::WatermarkBufferPtr createBuffer() override;
  Buffer::WatermarkBufferPtr& bufferedData() override;
  bool complete() override;
  bool has100Continueheaders() override;
  void do100ContinueHeaders() override;
  void doHeaders(bool end_stream) override;
  void doData(bool end_stream) override;
  void drainSavedResponseMetadata();
  void handleMetadataAfterHeadersCallback() override;

  void doMetadata() override {
    if (saved_response_metadata_ != nullptr) {
      drainSavedResponseMetadata();
    }
  }
  void doTrailers() override;
  bool hasTrailers() override;

  // Http::StreamEncoderFilterCallbacks
  void addEncodedData(Buffer::Instance& data, bool streaming) override;
  void injectEncodedDataToFilterChain(Buffer::Instance& data, bool end_stream) override;
  ResponseTrailerMap& addEncodedTrailers() override;
  void addEncodedMetadata(MetadataMapPtr&& metadata_map) override;
  void onEncoderFilterAboveWriteBufferHighWatermark() override;
  void onEncoderFilterBelowWriteBufferLowWatermark() override;
  void setEncoderBufferLimit(uint32_t limit) override;
  uint32_t encoderBufferLimit() override;
  void continueEncoding() override;
  const Buffer::Instance* encodingBuffer() override;
  void modifyEncodingBuffer(std::function<void(Buffer::Instance&)> callback) override;
  void sendLocalReply(Code code, absl::string_view body,
                      std::function<void(ResponseHeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details) override;
  Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override;

  void responseDataTooLarge();
  void responseDataDrained();

  StreamEncoderFilterSharedPtr handle_;
};

using ActiveStreamEncoderFilterPtr = std::unique_ptr<ActiveStreamEncoderFilter>;

/**
 * Callbacks invoked by the FilterManager to pass filter data/events back to the caller.
 */
class FilterManagerCallbacks {
public:
  virtual ~FilterManagerCallbacks() = default;

  /**
   * Called when the provided headers have been encoded by all the filters in the chain.
   * @param response_headers the encoded headers.
   * @param end_stream whether this is a header only response.
   */
  virtual void encodeHeaders(ResponseHeaderMap& response_headers, bool end_stream) PURE;

  /**
   * Called when the provided 100 Continue headers have been encoded by all the filters in the
   * chain.
   * @param response_headers the encoded headers.
   */
  virtual void encode100ContinueHeaders(ResponseHeaderMap& response_headers) PURE;

  /**
   * Called when the provided data has been encoded by all filters in the chain.
   * @param data the encoded data.
   * @param end_stream whether this is the end of the response.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called when the provided trailers have been encoded by all filters in the chain.
   * @param trailers the encoded trailers.
   */
  virtual void encodeTrailers(ResponseTrailerMap& trailers) PURE;

  /**
   * Called when the provided metadata has been encoded by all filters in the chain.
   * @param trailers the encoded trailers.
   */
  virtual void encodeMetadata(MetadataMapVector& metadata) PURE;

  /**
   * Injects request trailers into a stream that originally did not have request trailers.
   */
  virtual void setRequestTrailers(RequestTrailerMapPtr&& request_trailers) PURE;

  /**
   * Passes ownership of received continue headers to the parent. This may be called multiple times
   * in the case of multiple upstream calls.
   */
  virtual void setContinueHeaders(ResponseHeaderMapPtr&& response_headers) PURE;

  /**
   * Passes ownership of received response headers to the parent. This may be called multiple times
   * in the case of multiple upstream calls.
   */
  virtual void setResponseHeaders(ResponseHeaderMapPtr&& response_headers) PURE;

  /**
   * Passes ownership of received response trailers to the parent. This may be called multiple times
   * in the case of multiple upstream calls.
   */
  virtual void setResponseTrailers(ResponseTrailerMapPtr&& response_trailers) PURE;

  // TODO(snowp): We should consider moving filter access to headers/trailers to happen via the
  // callbacks instead of via the encode/decode callbacks on the filters.

  /**
   * The downstream request headers if set.
   */
  virtual RequestHeaderMapOptRef requestHeaders() PURE;

  /**
   * The downstream request trailers if present.
   */
  virtual RequestTrailerMapOptRef requestTrailers() PURE;

  /**
   * Retrieves a pointer to the continue headers set via the call to setContinueHeaders.
   */
  virtual ResponseHeaderMapOptRef continueHeaders() PURE;

  /**
   * Retrieves a pointer to the response headers set via the last call to setResponseHeaders.
   * Note that response headers might be set multiple times (e.g. if a local reply is issued after
   * headers have been received but before headers have been encoded), so it is not safe in general
   * to assume that any set of headers will be valid for the duration of a stream.
   */
  virtual ResponseHeaderMapOptRef responseHeaders() PURE;

  /**
   * Retrieves a pointer to the last response trailers set via setResponseTrailers.
   * Note that response trailers might be set multiple times, so it is not safe in general to assume
   * that any set of trailers will be valid for the duration of the stream.
   */
  virtual ResponseTrailerMapOptRef responseTrailers() PURE;

  /**
   * Called after encoding has completed.
   */
  virtual void endStream() PURE;

  /**
   * Called when the stream write buffer is no longer above the low watermark.
   */
  virtual void onDecoderFilterBelowWriteBufferLowWatermark() PURE;

  /**
   * Called when the stream write buffer is above above the high watermark.
   */
  virtual void onDecoderFilterAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when the FilterManager creates an Upgrade filter chain.
   */
  virtual void upgradeFilterChainCreated() PURE;

  /**
   * Called when request activity indicates that the request timeout should be disarmed.
   */
  virtual void disarmRequestTimeout() PURE;

  /**
   * Called when stream activity indicates that the stream idle timeout should be reset.
   */
  virtual void resetIdleTimer() PURE;

  /**
   * Called when the stream should be re-created, e.g. for an internal redirect.
   */
  virtual void recreateStream(StreamInfo::FilterStateSharedPtr filter_state) PURE;

  /**
   * Called when the stream should be reset.
   */
  virtual void resetStream() PURE;

  /**
   * Returns the upgrade map for the current route entry.
   */
  virtual const Router::RouteEntry::UpgradeMap* upgradeMap() PURE;

  /**
   * Returns the cluster info for the current route entry.
   */
  virtual Upstream::ClusterInfoConstSharedPtr clusterInfo() PURE;

  /**
   * Returns the current route.
   */
  virtual Router::RouteConstSharedPtr route(const Router::RouteCallback& cb) PURE;

  /**
   * Clears the cached route.
   */
  virtual void clearRouteCache() PURE;

  /**
   * Returns the current route configuration.
   */
  virtual absl::optional<Router::ConfigConstSharedPtr> routeConfig() PURE;

  /**
   * Update the current route configuration.
   */
  virtual void
  requestRouteConfigUpdate(Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) PURE;

  /**
   * Returns the current active span.
   */
  virtual Tracing::Span& activeSpan() PURE;

  // TODO(snowp): It might make more sense to pass (optional?) counters to the FM instead of
  // calling back out to the AS to record them.
  /**
   * Called when a stream fails due to the response data being too large.
   */
  virtual void onResponseDataTooLarge() PURE;

  /**
   * Called when a stream fails due to the request data being too large.
   */
  virtual void onRequestDataTooLarge() PURE;

  /**
   * Returns the Http1StreamEncoderOptions associated with the response encoder.
   */
  virtual Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() PURE;

  /**
   * Called when a local reply is made by the filter manager.
   * @param code the response code of the local reply.
   */
  virtual void onLocalReply(Code code) PURE;

  /**
   * Returns the tracing configuration to use for this stream.
   */
  virtual Tracing::Config& tracingConfig() PURE;

  /**
   * Returns the tracked scope to use for this stream.
   */
  virtual const ScopeTrackedObject& scope() PURE;
};

/**
 * FilterManager manages decoding a request through a series of decoding filter and the encoding
 * of the resulting response.
 */
class FilterManager : public ScopeTrackedObject,
                      FilterChainFactoryCallbacks,
                      Logger::Loggable<Logger::Id::http> {
public:
  FilterManager(FilterManagerCallbacks& filter_manager_callbacks, Event::Dispatcher& dispatcher,
                const Network::Connection& connection, uint64_t stream_id, bool proxy_100_continue,
                uint32_t buffer_limit, FilterChainFactory& filter_chain_factory,
                const LocalReply::LocalReply& local_reply, Http::Protocol protocol,
                TimeSource& time_source, StreamInfo::FilterStateSharedPtr parent_filter_state,
                StreamInfo::FilterState::LifeSpan filter_state_life_span)
      : filter_manager_callbacks_(filter_manager_callbacks), dispatcher_(dispatcher),
        connection_(connection), stream_id_(stream_id), proxy_100_continue_(proxy_100_continue),
        buffer_limit_(buffer_limit), filter_chain_factory_(filter_chain_factory),
        local_reply_(local_reply),
        stream_info_(protocol, time_source, parent_filter_state, filter_state_life_span) {}
  ~FilterManager() override {
    ASSERT(state_.destroyed_);
    ASSERT(state_.filter_call_state_ == 0);
  }

  // ScopeTrackedObject
  void dumpState(std::ostream& os, int indent_level = 0) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "FilterManager " << this << DUMP_MEMBER(state_.has_continue_headers_) << "\n";

    DUMP_OPT_REF_DETAILS(filter_manager_callbacks_.requestHeaders());
    DUMP_OPT_REF_DETAILS(filter_manager_callbacks_.requestTrailers());
    DUMP_OPT_REF_DETAILS(filter_manager_callbacks_.responseHeaders());
    DUMP_OPT_REF_DETAILS(filter_manager_callbacks_.responseTrailers());
    DUMP_DETAILS(&stream_info_);
  }

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

  void log() {
    RequestHeaderMap* request_headers = nullptr;
    if (filter_manager_callbacks_.requestHeaders()) {
      request_headers = &filter_manager_callbacks_.requestHeaders()->get();
    }
    ResponseHeaderMap* response_headers = nullptr;
    if (filter_manager_callbacks_.responseHeaders()) {
      response_headers = &filter_manager_callbacks_.responseHeaders()->get();
    }
    ResponseTrailerMap* response_trailers = nullptr;
    if (filter_manager_callbacks_.responseTrailers()) {
      response_trailers = &filter_manager_callbacks_.responseTrailers()->get();
    }

    for (const auto& log_handler : access_log_handlers_) {
      log_handler->log(request_headers, response_headers, response_trailers, stream_info_);
    }
  }

  void onStreamComplete() {
    for (auto& filter : decoder_filters_) {
      filter->handle_->onStreamComplete();
    }

    for (auto& filter : encoder_filters_) {
      // Do not call onStreamComplete twice for dual registered filters.
      if (!filter->dual_filter_) {
        filter->handle_->onStreamComplete();
      }
    }
  }

  void destroyFilters() {
    state_.destroyed_ = true;

    for (auto& filter : decoder_filters_) {
      filter->handle_->onDestroy();
    }

    for (auto& filter : encoder_filters_) {
      // Do not call on destroy twice for dual registered filters.
      if (!filter->dual_filter_) {
        filter->handle_->onDestroy();
      }
    }
  }

  /**
   * Decodes the provided headers starting at the first filter in the chain.
   * @param headers the headers to decode.
   * @param end_stream whether the request is header only.
   */
  void decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    decodeHeaders(nullptr, headers, end_stream);
  }

  /**
   * Decodes the provided data starting at the first filter in the chain.
   * @param data the data to decode.
   * @param end_stream whether this data is the end of the request.
   */
  void decodeData(Buffer::Instance& data, bool end_stream) {
    decodeData(nullptr, data, end_stream, FilterIterationStartState::CanStartFromCurrent);
  }

  /**
   * Decodes the provided trailers starting at the first filter in the chain.
   * @param trailers the trailers to decode.
   */
  void decodeTrailers(RequestTrailerMap& trailers) { decodeTrailers(nullptr, trailers); }

  /**
   * Decodes the provided metadata starting at the first filter in the chain.
   * @param metadata_map the metadata to decode.
   */
  void decodeMetadata(MetadataMap& metadata_map) { decodeMetadata(nullptr, metadata_map); }

  // TODO(snowp): Make private as filter chain construction is moved into FM.
  void addStreamDecoderFilterWorker(StreamDecoderFilterSharedPtr filter, bool dual_filter);
  void addStreamEncoderFilterWorker(StreamEncoderFilterSharedPtr filter, bool dual_filter);

  void disarmRequestTimeout();

  /**
   * If end_stream is true, marks decoding as complete. This is a noop if end_stream is false.
   * @param end_stream whether decoding is complete.
   */
  void maybeEndDecode(bool end_stream);

  /**
   * If end_stream is true, marks encoding as complete. This is a noop if end_stream is false.
   * @param end_stream whether encoding is complete.
   */
  void maybeEndEncode(bool end_stream);

  void sendLocalReply(bool is_grpc_request, Code code, absl::string_view body,
                      const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details);
  /**
   * Sends a local reply by constructing a response and passing it through all the encoder
   * filters. The resulting response will be passed out via the FilterManagerCallbacks.
   */
  void sendLocalReplyViaFilterChain(
      bool is_grpc_request, Code code, absl::string_view body,
      const std::function<void(ResponseHeaderMap& headers)>& modify_headers, bool is_head_request,
      const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details);

  /**
   * Sends a local reply by constructing a response and skipping the encoder filters. The
   * resulting response will be passed out via the FilterManagerCallbacks.
   */
  void sendDirectLocalReply(Code code, absl::string_view body,
                            const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
                            bool is_head_request,
                            const absl::optional<Grpc::Status::GrpcStatus> grpc_status);

  // Possibly increases buffer_limit_ to the value of limit.
  void setBufferLimit(uint32_t limit);

  /**
   * @return bool whether any above high watermark triggers are currently active
   */
  bool aboveHighWatermark() { return high_watermark_count_ != 0; }

  // Pass on watermark callbacks to watermark subscribers. This boils down to passing watermark
  // events for this stream and the downstream connection to the router filter.
  void callHighWatermarkCallbacks();
  void callLowWatermarkCallbacks();

  void requestHeadersInitialized() {
    if (Http::Headers::get().MethodValues.Head ==
        filter_manager_callbacks_.requestHeaders()->get().getMethodValue()) {
      state_.is_head_request_ = true;
    }
    state_.is_grpc_request_ =
        Grpc::Common::isGrpcRequestHeaders(filter_manager_callbacks_.requestHeaders()->get());
  }

  /**
   * Marks local processing as complete.
   */
  void setLocalComplete() { state_.local_complete_ = true; }

  /**
   * Whether the filters have been destroyed.
   */
  bool destroyed() const { return state_.destroyed_; }

  /**
   * Whether remote processing has been marked as complete.
   */
  bool remoteComplete() const { return state_.remote_complete_; }

  /**
   * Instructs the FilterManager to not create a filter chain. This makes it possible to issue
   * a local reply without the overhead of creating and traversing the filters.
   */
  void skipFilterChainCreation() {
    ASSERT(!state_.created_filter_chain_);
    state_.created_filter_chain_ = true;
  }

  // TODO(snowp): This should probably return a StreamInfo instead of the impl.
  StreamInfo::StreamInfoImpl& streamInfo() { return stream_info_; }
  const StreamInfo::StreamInfoImpl& streamInfo() const { return stream_info_; }

  // Set up the Encoder/Decoder filter chain.
  bool createFilterChain();

  const Network::Connection* connection() const { return &connection_; }

  uint64_t streamId() const { return stream_id_; }

private:
  // Indicates which filter to start the iteration with.
  enum class FilterIterationStartState { AlwaysStartFromNext, CanStartFromCurrent };

  // Returns the encoder filter to start iteration with.
  std::list<ActiveStreamEncoderFilterPtr>::iterator
  commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream,
                     FilterIterationStartState filter_iteration_start_state);
  // Returns the decoder filter to start iteration with.
  std::list<ActiveStreamDecoderFilterPtr>::iterator
  commonDecodePrefix(ActiveStreamDecoderFilter* filter,
                     FilterIterationStartState filter_iteration_start_state);
  void addDecodedData(ActiveStreamDecoderFilter& filter, Buffer::Instance& data, bool streaming);
  RequestTrailerMap& addDecodedTrailers();
  MetadataMapVector& addDecodedMetadata();
  // Helper function for the case where we have a header only request, but a filter adds a body
  // to it.
  void maybeContinueDecoding(
      const std::list<ActiveStreamDecoderFilterPtr>::iterator& maybe_continue_data_entry);
  void decodeHeaders(ActiveStreamDecoderFilter* filter, RequestHeaderMap& headers, bool end_stream);
  // Sends data through decoding filter chains. filter_iteration_start_state indicates which
  // filter to start the iteration with.
  void decodeData(ActiveStreamDecoderFilter* filter, Buffer::Instance& data, bool end_stream,
                  FilterIterationStartState filter_iteration_start_state);
  void decodeTrailers(ActiveStreamDecoderFilter* filter, RequestTrailerMap& trailers);
  void decodeMetadata(ActiveStreamDecoderFilter* filter, MetadataMap& metadata_map);
  void addEncodedData(ActiveStreamEncoderFilter& filter, Buffer::Instance& data, bool streaming);
  ResponseTrailerMap& addEncodedTrailers();
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

  // Returns true if new metadata is decoded. Otherwise, returns false.
  bool processNewlyAddedMetadata();

  // Returns true if filter has stopped iteration for all frame types. Otherwise, returns false.
  // filter_streaming is the variable to indicate if stream is streaming, and its value may be
  // changed by the function.
  bool handleDataIfStopAll(ActiveStreamFilterBase& filter, Buffer::Instance& data,
                           bool& filter_streaming);

  MetadataMapVector* getRequestMetadataMapVector() {
    if (request_metadata_map_vector_ == nullptr) {
      request_metadata_map_vector_ = std::make_unique<MetadataMapVector>();
    }
    return request_metadata_map_vector_.get();
  }

  FilterManagerCallbacks& filter_manager_callbacks_;
  Event::Dispatcher& dispatcher_;
  const Network::Connection& connection_;
  const uint64_t stream_id_;
  const bool proxy_100_continue_;

  std::list<ActiveStreamDecoderFilterPtr> decoder_filters_;
  std::list<ActiveStreamEncoderFilterPtr> encoder_filters_;
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_;

  // Stores metadata added in the decoding filter that is being processed. Will be cleared before
  // processing the next filter. The storage is created on demand. We need to store metadata
  // temporarily in the filter in case the filter has stopped all while processing headers.
  std::unique_ptr<MetadataMapVector> request_metadata_map_vector_;
  Buffer::WatermarkBufferPtr buffered_response_data_;
  Buffer::WatermarkBufferPtr buffered_request_data_;
  uint32_t buffer_limit_{0};
  uint32_t high_watermark_count_{0};
  std::list<DownstreamWatermarkCallbacks*> watermark_callbacks_;
  Network::Socket::OptionsSharedPtr upstream_options_ =
      std::make_shared<Network::Socket::Options>();

  FilterChainFactory& filter_chain_factory_;
  const LocalReply::LocalReply& local_reply_;
  StreamInfo::StreamInfoImpl stream_info_;
  // TODO(snowp): Once FM has been moved to its own file we'll make these private classes of FM,
  // at which point they no longer need to be friends.
  friend ActiveStreamFilterBase;
  friend ActiveStreamDecoderFilter;
  friend ActiveStreamEncoderFilter;

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

  struct State {
    State()
        : remote_complete_(false), local_complete_(false), has_continue_headers_(false),
          created_filter_chain_(false), is_head_request_(false), is_grpc_request_(false),
          non_100_response_headers_encoded_(false) {}

    uint32_t filter_call_state_{0};

    bool remote_complete_ : 1;
    bool local_complete_ : 1; // This indicates that local is complete prior to filter processing.
                              // A filter can still stop the stream from being complete as seen
                              // by the codec.
    // By default, we will assume there are no 100-Continue headers. If encode100ContinueHeaders
    // is ever called, this is set to true so commonContinue resumes processing the 100-Continue.
    bool has_continue_headers_ : 1;
    bool created_filter_chain_ : 1;
    // These two are latched on initial header read, to determine if the original headers
    // constituted a HEAD or gRPC request, respectively.
    bool is_head_request_ : 1;
    bool is_grpc_request_ : 1;
    // Tracks if headers other than 100-Continue have been encoded to the codec.
    bool non_100_response_headers_encoded_ : 1;

    // The following 3 members are booleans rather than part of the space-saving bitfield as they
    // are passed as arguments to functions expecting bools. Extend State using the bitfield
    // where possible.
    bool encoder_filters_streaming_{true};
    bool decoder_filters_streaming_{true};
    bool destroyed_{false};

    // Used to track which filter is the latest filter that has received data.
    ActiveStreamEncoderFilter* latest_data_encoding_filter_{};
    ActiveStreamDecoderFilter* latest_data_decoding_filter_{};
  };

  State state_;
};

} // namespace Http
} // namespace Envoy
