#pragma once

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/dump_state_utils.h"
#include "common/common/linked_object.h"
#include "common/grpc/common.h"
#include "common/local_reply/local_reply.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Http {

class FilterManagerCallbacks {
public:
  virtual ~FilterManagerCallbacks() = default;

  virtual void onSpanFinalized(Tracing::Span& span, Http::RequestHeaderMap* request_headers,
                               Http::ResponseHeaderMap* response_headers,
                               Http::ResponseTrailerMap* response_trailers) PURE;

  virtual const Network::Connection* connection() const PURE;
  virtual uint64_t streamId() const PURE;
  virtual void refreshIdleTimeout() PURE;
  virtual void finalizeHeaders(Http::ResponseHeaderMap& response_headers, bool end_stream) PURE;
  virtual void finalize100ContinueHeaders(Http::ResponseHeaderMap& response_headers) PURE;
  virtual absl::optional<Router::RouteConstSharedPtr> cachedRoute() PURE;
  virtual void upgradeFilterChainCreated() PURE;
  virtual void endStream() PURE;
  virtual Tracing::Config& tracingConfig() PURE;
  virtual absl::optional<Upstream::ClusterInfoConstSharedPtr> cachedClusterInfo() PURE;
  virtual void refreshCachedRoute() PURE;
  virtual void refreshCachedRoute(const Router::RouteCallback& cb) PURE;
  virtual void clearRouteCache() PURE;
  virtual void recreateStream(RequestHeaderMapPtr request_headers) PURE;
  virtual void
  requestRouteConfigUpdate(Event::Dispatcher& dispatcher,
                           Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) PURE;

  virtual absl::optional<Router::ConfigConstSharedPtr> routeConfig() PURE;
  virtual void onFilterAboveWriteBufferHighWatermark() PURE;
  virtual void onFilterBelowWriteBufferLowWatermark() PURE;
};

// Manages an iteration of a set of HTTP decoder/encoder filters.
class FilterManager : public FilterChainFactoryCallbacks,
                      public ScopeTrackedObject,
                      Logger::Loggable<Logger::Id::http> {
public:
  FilterManager(uint64_t stream_id, Http::Protocol protocol, TimeSource& time_source,
                StreamInfo::FilterStateSharedPtr parent_filter_state,
                FilterManagerCallbacks& callbacks, const LocalReply::LocalReply& local_reply,
                FilterChainFactory& filter_chain_factory, Event::Dispatcher& dispatcher,
                bool proxy_100_continue)
      : stream_id_(stream_id), stream_info_(protocol, time_source, parent_filter_state,
                                            StreamInfo::FilterState::LifeSpan::Connection),
        callbacks_(callbacks), local_reply_(local_reply),
        filter_chain_factory_(filter_chain_factory), dispatcher_(dispatcher),
        proxy_100_continue_(proxy_100_continue) {}

  ~FilterManager() {
    // TODO(snowp): Add all these access loggers to access_log_handlers_
    // for (const AccessLog::InstanceSharedPtr& access_log :
    //      connection_manager_.config_.accessLogs()) {
    //   access_log->log(request_headers_.get(), response_headers_.get(), response_trailers_.get(),
    //                   stream_info_);
    // }
    for (const auto& log_handler : access_log_handlers_) {
      log_handler->log(request_headers_.get(), response_headers_.get(), response_trailers_.get(),
                       stream_info_);
    }

    if (active_span_) {
      callbacks_.onSpanFinalized(*active_span_, request_headers_.get(), response_headers_.get(),
                                 response_trailers_.get());
    }

    ASSERT(state_.filter_call_state_ == 0);
  }

  void setResponseEncoder(ResponseEncoder* response_encoder) {
    response_encoder_ = response_encoder;
    buffer_limit_ = response_encoder->getStream().bufferLimit();
  }

  StreamInfo::StreamInfo& streamInfo() { return stream_info_; }

  void traceRequest(Tracing::SpanPtr span, Tracing::OperationName operation_name,
                    absl::optional<Router::RouteConstSharedPtr> route) {
    active_span_ = std::move(span);
    if (!active_span_) {
      return;
    }

    // TODO: Need to investigate the following code based on the cached route, as may
    // be broken in the case a filter changes the route.

    // If a decorator has been defined, apply it to the active span.
    if (route.has_value() && route.value()->decorator()) {
      const Router::Decorator* decorator = route.value()->decorator();

      decorator->apply(*active_span_);

      state_.decorated_propagate_ = decorator->propagate();

      // Cache decorated operation.
      if (!decorator->getOperation().empty()) {
        decorated_operation_ = &decorator->getOperation();
      }
    }

    if (operation_name == Tracing::OperationName::Egress) {
      // For egress (outbound) requests, pass the decorator's operation name (if defined and
      // propagation enabled) as a request header to enable the receiving service to use it in its
      // server span.
      if (decorated_operation_ && state_.decorated_propagate_) {
        request_headers_->setEnvoyDecoratorOperation(*decorated_operation_);
      }
    } else {
      const HeaderEntry* req_operation_override = request_headers_->EnvoyDecoratorOperation();

      // For ingress (inbound) requests, if a decorator operation name has been provided, it
      // should be used to override the active span's operation.
      if (req_operation_override) {
        if (!req_operation_override->value().empty()) {
          active_span_->setOperation(req_operation_override->value().getStringView());

          // Clear the decorated operation so won't be used in the response header, as
          // it has been overridden by the inbound decorator operation request header.
          decorated_operation_ = nullptr;
        }
        // Remove header so not propagated to service
        request_headers_->removeEnvoyDecoratorOperation();
      }
    }
  }

  bool maybeEndStream() {
    bool reset_stream = false;
    // If the response encoder is still associated with the stream, reset the stream. The exception
    // here is when Envoy "ends" the stream by calling recreateStream at which point recreateStream
    // explicitly nulls out response_encoder to avoid the downstream being notified of the
    // Envoy-internal stream instance being ended.
    if (response_encoder_ != nullptr &&
        (!state_.remote_complete_ || !state_.codec_saw_local_complete_)) {
      // Indicate local is complete at this point so that if we reset during a continuation, we
      // don't raise further data or trailers.
      state_.local_complete_ = true;
      state_.codec_saw_local_complete_ = true;
      reset_stream = true;
    }

    return reset_stream;
  }

  void doDeferredStreamDestroy() {
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

    disarmRequestTimeout();
  }

  void addStreamDecoderFilterWorker(StreamDecoderFilterSharedPtr filter, bool dual_filter);
  void addStreamEncoderFilterWorker(StreamEncoderFilterSharedPtr filter, bool dual_filter);

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

  void decodeHeaders(bool end_stream) { decodeHeaders(nullptr, *request_headers_, end_stream); }
  void decodeData(Buffer::Instance& data, bool end_stream) {
    decodeData(nullptr, data, end_stream, FilterIterationStartState::CanStartFromCurrent);
  }
  void decodeMetadata(MetadataMap& metadata_map) { decodeMetadata(nullptr, metadata_map); }
  void decodeTrailers() { decodeTrailers(nullptr, *request_trailers_); }

  // Pass on watermark callbacks to watermark subscribers. This boils down to passing watermark
  // events for this stream and the downstream connection to the router filter.
  void callHighWatermarkCallbacks();
  void callLowWatermarkCallbacks();

private:
  /**
   * Base class wrapper for both stream encoder and decoder filters.
   */
  struct ActiveStreamFilterBase : public virtual StreamFilterCallbacks {
    ActiveStreamFilterBase(FilterManager& parent, bool dual_filter)
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

  // Indicates which filter to start the iteration with.
  enum class FilterIterationStartState { AlwaysStartFromNext, CanStartFromCurrent };

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
                         FilterIterationStartState::CanStartFromCurrent);
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
    ActiveStreamEncoderFilter(FilterManager& parent, StreamEncoderFilterSharedPtr filter,
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
                         FilterIterationStartState::CanStartFromCurrent);
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

public:
  void maybeEndDecode(bool end_stream);

private:
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

  // This is a helper function for encodeData and responseDataTooLarge which allows for shared
  // code for the two data encoding paths. It does stats updates and tracks potential end of
  // stream.
  void encodeDataInternal(Buffer::Instance& data, bool end_stream);

public:
  void maybeEndEncode(bool end_stream);

  void sendLocalReply(bool is_grpc_request, Code code, absl::string_view body,
                      const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
                      bool is_head_request,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details);

private:
  void addDecodedData(ActiveStreamDecoderFilter& filter, Buffer::Instance& data, bool streaming);
  RequestTrailerMap& addDecodedTrailers();
  MetadataMapVector& addDecodedMetadata();

  // Returns true if filter has stopped iteration for all frame types. Otherwise, returns false.
  // filter_streaming is the variable to indicate if stream is streaming, and its value may be
  // changed by the function.
  bool handleDataIfStopAll(ActiveStreamFilterBase& filter, Buffer::Instance& data,
                           bool& filter_streaming);

  // Returns the encoder filter to start iteration with.
  std::list<ActiveStreamEncoderFilterPtr>::iterator
  commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream,
                     FilterIterationStartState filter_iteration_start_state);
  // Returns the decoder filter to start iteration with.
  std::list<ActiveStreamDecoderFilterPtr>::iterator
  commonDecodePrefix(ActiveStreamDecoderFilter* filter,
                     FilterIterationStartState filter_iteration_start_state);
  // Possibly increases buffer_limit_ to the value of limit.
  void setBufferLimit(uint32_t limit);

public:
  // Set up the Encoder/Decoder filter chain.
  bool createFilterChain();

private:
  MetadataMapVector* getRequestMetadataMapVector() {
    if (request_metadata_map_vector_ == nullptr) {
      request_metadata_map_vector_ = std::make_unique<MetadataMapVector>();
    }
    return request_metadata_map_vector_.get();
  }

  // Returns true if new metadata is decoded. Otherwise, returns false.
  bool processNewlyAddedMetadata();

  void disarmRequestTimeout();

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

  // All state for the filter iteration. Put here for readability.
  struct State {
    State()
        : created_filter_chain_(false), successful_upgrade_(false), is_head_request_(false),
          has_continue_headers_(false), codec_saw_local_complete_(false), remote_complete_(false),
          local_complete_(false), decorated_propagate_(false) {}

    uint32_t filter_call_state_{0};
    bool created_filter_chain_ : 1;
    bool successful_upgrade_ : 1;
    bool is_head_request_ : 1;
    // By default, we will assume there are no 100-Continue headers. If encode100ContinueHeaders
    // is ever called, this is set to true so commonContinue resumes processing the 100-Continue.
    bool has_continue_headers_ : 1;
    // Whether a filter has indicated that the request should be treated as a headers only
    // request.
    bool decoding_headers_only_{false};
    // Whether a filter has indicated that the response should be treated as a headers only
    // response.
    bool encoding_headers_only_{false};

    bool codec_saw_local_complete_ : 1; // This indicates that local is complete as written all
                                        // the way through to the codec.
    bool remote_complete_ : 1;
    bool local_complete_ : 1; // This indicates that local is complete prior to filter processing.
                              // A filter can still stop the stream from being complete as seen
                              // by the codec.

    bool decorated_propagate_ : 1;
    bool encoder_filters_streaming_{true};
    bool decoder_filters_streaming_{true};
    bool destroyed_{false};

    // Used to track which filter is the latest filter that has received data.
    ActiveStreamEncoderFilter* latest_data_encoding_filter_{};
    ActiveStreamDecoderFilter* latest_data_decoding_filter_{};
  };

  // Stores metadata added in the decoding filter that is being processed. Will be cleared before
  // processing the next filter. The storage is created on demand. We need to store metadata
  // temporarily in the filter in case the filter has stopped all while processing headers.
  std::unique_ptr<MetadataMapVector> request_metadata_map_vector_{nullptr};
  uint32_t buffer_limit_{0};
  Network::Socket::OptionsSharedPtr upstream_options_;
  ResponseEncoder* response_encoder_{};

public:
  State state_{};

private:
  const uint_fast64_t stream_id_;
  ResponseHeaderMapPtr continue_headers_;

public:
  ResponseHeaderMapPtr response_headers_;

private:
  Buffer::WatermarkBufferPtr buffered_response_data_;
  ResponseTrailerMapPtr response_trailers_{};

public:
  RequestHeaderMapPtr request_headers_;

private:
  Buffer::WatermarkBufferPtr buffered_request_data_;

public:
  RequestTrailerMapPtr request_trailers_;
  Tracing::SpanPtr active_span_;

private:
  std::list<ActiveStreamDecoderFilterPtr> decoder_filters_;
  std::list<ActiveStreamEncoderFilterPtr> encoder_filters_;
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_;

public:
  const std::string* decorated_operation_{nullptr};

private:
  StreamInfo::StreamInfoImpl stream_info_;
  FilterManagerCallbacks& callbacks_;

public:
  // Per-stream request timeout.
  Event::TimerPtr request_timer_;
  const LocalReply::LocalReply& local_reply_;
  FilterChainFactory& filter_chain_factory_;
  Event::Dispatcher& dispatcher_;

public:
  uint32_t high_watermark_count_{0};

private:
  std::list<DownstreamWatermarkCallbacks*> watermark_callbacks_{};
  const bool proxy_100_continue_;
};

} // namespace Http
} // namespace Envoy