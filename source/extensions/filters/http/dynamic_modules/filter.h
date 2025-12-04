#pragma once

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

using namespace Envoy::Http;

/**
 * A filter that uses a dynamic module and corresponds to a single HTTP stream.
 */
class DynamicModuleHttpFilter : public Http::StreamFilter,
                                public std::enable_shared_from_this<DynamicModuleHttpFilter>,
                                public Logger::Loggable<Logger::Id::dynamic_modules>,
                                public Http::DownstreamWatermarkCallbacks {
public:
  DynamicModuleHttpFilter(DynamicModuleHttpFilterConfigSharedPtr config,
                          Stats::SymbolTable& symbol_table)
      : config_(config), stat_name_pool_(symbol_table) {}
  ~DynamicModuleHttpFilter() override;

  /**
   * Initializes the in-module filter.
   */
  void initializeInModuleFilter();

  // ---------- Http::StreamFilterBase ------------
  void onStreamComplete() override;
  void onDestroy() override;

  // ----------  Http::StreamDecoderFilter  ----------
  FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(RequestTrailerMap&) override;
  FilterMetadataStatus decodeMetadata(MetadataMap&) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    // config_ can only be nullptr in certain unit tests where we don't set up the
    // whole filter chain.
    if (config_ && config_->terminal_filter_) {
      decoder_callbacks_->addDownstreamWatermarkCallbacks(*this);
    }
  }
  void decodeComplete() override;

  // ----------  Http::StreamEncoderFilter  ----------
  Filter1xxHeadersStatus encode1xxHeaders(ResponseHeaderMap&) override;
  FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) override;
  FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus encodeTrailers(ResponseTrailerMap&) override;
  FilterMetadataStatus encodeMetadata(MetadataMap&) override;
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }
  void encodeComplete() override;

  bool isDestroyed() const { return destroyed_; }

  // ----------  Http::DownstreamWatermarkCallbacks  ----------
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  void sendLocalReply(Code code, absl::string_view body,
                      std::function<void(ResponseHeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details);

  // The callbacks for the filter. They are only valid until onDestroy() is called.
  StreamDecoderFilterCallbacks* decoder_callbacks_ = nullptr;
  StreamEncoderFilterCallbacks* encoder_callbacks_ = nullptr;
  bool destroyed_ = false;

  // These are used to hold the current chunk of the request/response body during the decodeData and
  // encodeData callbacks. It is only valid during the call and should not be used outside of the
  // call.
  Buffer::Instance* current_request_body_ = nullptr;
  Buffer::Instance* current_response_body_ = nullptr;

  /**
   * Helper to get the correct callbacks.
   */
  Http::StreamFilterCallbacks* callbacks() {
    if (decoder_callbacks_) {
      return decoder_callbacks_;
    } else if (encoder_callbacks_) {
      return encoder_callbacks_;
    } else {
      return nullptr;
    }
  }

  RequestHeaderMapOptRef requestHeaders() {
    if (decoder_callbacks_) {
      return decoder_callbacks_->requestHeaders();
    }
    return absl::nullopt;
  }

  RequestTrailerMapOptRef requestTrailers() {
    if (decoder_callbacks_) {
      return decoder_callbacks_->requestTrailers();
    }
    return absl::nullopt;
  }

  ResponseHeaderMapOptRef responseHeaders() {
    if (encoder_callbacks_) {
      return encoder_callbacks_->responseHeaders();
    }
    return absl::nullopt;
  }

  ResponseTrailerMapOptRef responseTrailers() {
    if (encoder_callbacks_) {
      return encoder_callbacks_->responseTrailers();
    }
    return absl::nullopt;
  }

  /**
   * Helper to get the downstream information of the stream.
   */
  StreamInfo::StreamInfo* streamInfo() {
    auto cb = callbacks();
    if (cb) {
      return &cb->streamInfo();
    }
    return nullptr;
  }

  /**
   * Helper to get the upstream information of the stream.
   */
  StreamInfo::UpstreamInfo* upstreamInfo() {
    auto stream_info = streamInfo();
    if (stream_info) {
      return stream_info->upstreamInfo().get();
    }
    return nullptr;
  }

  /**
   * Helper to get the connection information
   */
  OptRef<const Network::Connection> connection() {
    auto cb = callbacks();
    if (cb == nullptr) {
      return {};
    }
    return cb->connection();
  }

  /**
   * This is called when an event is scheduled via DynamicModuleHttpFilterScheduler::commit.
   */
  void onScheduled(uint64_t event_id);

  /**
   * This can be used to continue the decoding of the HTTP request after the processing has been
   * stopped at the normal HTTP event hooks such as decodeHeaders or encodeHeaders.
   */
  void continueDecoding();

  /**
   * This can be used to continue the encoding of the HTTP response after the processing has been
   * stopped at the normal HTTP event hooks such as encodeHeaders or encodeData.
   */
  void continueEncoding();

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint32_t callout_id, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

  /**
   * Starts a streamable HTTP callout to the specified cluster with the given message.
   * Returns a stream handle that can be used to reset the stream.
   */
  envoy_dynamic_module_type_http_callout_init_result
  startHttpStream(envoy_dynamic_module_type_http_stream_envoy_ptr* stream_ptr_out,
                  absl::string_view cluster_name, Http::RequestMessagePtr&& message,
                  bool end_stream, uint64_t timeout_milliseconds);

  /**
   * Resets an ongoing streamable HTTP callout stream.
   */
  void resetHttpStream(envoy_dynamic_module_type_http_stream_envoy_ptr stream_ptr);

  /**
   * Sends data on an ongoing streamable HTTP callout stream.
   */
  bool sendStreamData(envoy_dynamic_module_type_http_stream_envoy_ptr stream_ptr,
                      Buffer::Instance& data, bool end_stream);

  /**
   * Sends trailers on an ongoing streamable HTTP callout stream.
   */
  bool sendStreamTrailers(envoy_dynamic_module_type_http_stream_envoy_ptr stream_ptr,
                          Http::RequestTrailerMapPtr trailers);

  const DynamicModuleHttpFilterConfig& getFilterConfig() const { return *config_; }
  Stats::StatNameDynamicPool& getStatNamePool() { return stat_name_pool_; }

private:
  /**
   * This is a helper function to get the `this` pointer as a void pointer which is passed to the
   * various event hooks.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Called when filter is destroyed via onDestroy() or destructor. Forwards the call to the
   * module via on_http_filter_destroy_ and resets in_module_filter_ to null. Subsequent calls are a
   * no-op.
   */
  void destroy();

  // True if the filter is in the continue state. This is to avoid prohibited calls to
  // continueDecoding() or continueEncoding() multiple times.
  bool in_continue_ = false;

  // This helps to avoid reentering the module when sending a local reply. For example, if
  // sendLocalReply() is called, encodeHeaders and encodeData will be called again inline on top of
  // the stack calling it, which can be problematic. For example, with Rust, that might cause
  // multiple mutable borrows of the same object. In practice, a module shouldn't need encodeHeaders
  // and encodeData to be called for local reply contents, so we just skip them with this flag.
  bool sent_local_reply_ = false;

  const DynamicModuleHttpFilterConfigSharedPtr config_ = nullptr;
  envoy_dynamic_module_type_http_filter_module_ptr in_module_filter_ = nullptr;
  Stats::StatNameDynamicPool stat_name_pool_;

  /**
   * This implementation of the AsyncClient::Callbacks is used to handle the response from the HTTP
   * callout from the parent HTTP filter.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(std::shared_ptr<DynamicModuleHttpFilter> filter, uint32_t id)
        : filter_(std::move(filter)), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const AsyncClient::Request& request, ResponseMessagePtr&& response) override;
    void onFailure(const AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};
    // This is the request object that is used to send the HTTP callout. It is used to cancel the
    // callout if the filter is destroyed before the callout is completed.
    Http::AsyncClient::Request* request_ = nullptr;

  private:
    std::shared_ptr<DynamicModuleHttpFilter> filter_;
    uint32_t callout_id_;
  };

  /**
   * This implementation of the AsyncClient::StreamCallbacks is used to handle the streaming
   * response from the HTTP streamable callout from the parent HTTP filter.
   */
  class HttpStreamCalloutCallback : public Http::AsyncClient::StreamCallbacks,
                                    public Event::DeferredDeletable {
  public:
    HttpStreamCalloutCallback(std::shared_ptr<DynamicModuleHttpFilter> filter)
        : this_as_void_ptr_(static_cast<void*>(this)), filter_(std::move(filter)) {}
    ~HttpStreamCalloutCallback() override = default;

    // AsyncClient::StreamCallbacks
    void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void onData(Buffer::Instance& data, bool end_stream) override;
    void onTrailers(ResponseTrailerMapPtr&& trailers) override;
    void onComplete() override;
    void onReset() override;

    // This is the stream object that is used to send the streaming HTTP callout. It is used to
    // reset the callout if the filter is destroyed before the callout is completed or if the
    // module requests it.
    Http::AsyncClient::Stream* stream_ = nullptr;

    // Store the request message to keep headers alive, since AsyncStream stores a pointer to them.
    Http::RequestMessagePtr request_message_ = nullptr;

    // Store the request trailers to keep them alive, since AsyncStream stores a pointer to them.
    Http::RequestTrailerMapPtr request_trailers_ = nullptr;

    // Store this as void* so it can be passed directly to the module without casting.
    void* this_as_void_ptr_;

    // Track if this callback has already been cleaned up to avoid double cleanup.
    bool cleaned_up_ = false;

  private:
    std::shared_ptr<DynamicModuleHttpFilter> filter_;
  };

  absl::flat_hash_map<uint32_t, std::unique_ptr<DynamicModuleHttpFilter::HttpCalloutCallback>>
      http_callouts_;

  // Unlike http_callouts_, we don't use an id-based map because the stream pointer itself is the
  // unique identifier. We store the callback objects here to manage their lifetime.
  absl::flat_hash_map<void*, std::unique_ptr<DynamicModuleHttpFilter::HttpStreamCalloutCallback>>
      http_stream_callouts_;
};

using DynamicModuleHttpFilterSharedPtr = std::shared_ptr<DynamicModuleHttpFilter>;
using DynamicModuleHttpFilterWeakPtr = std::weak_ptr<DynamicModuleHttpFilter>;

/**
 * This class is used to schedule a HTTP filter event hook from a different thread
 * than the one it was assigned to. This is created via
 * envoy_dynamic_module_callback_http_filter_scheduler_new and deleted via
 * envoy_dynamic_module_callback_http_filter_scheduler_delete.
 */
class DynamicModuleHttpFilterScheduler {
public:
  DynamicModuleHttpFilterScheduler(DynamicModuleHttpFilterWeakPtr filter,
                                   Event::Dispatcher& dispatcher)
      : filter_(std::move(filter)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([filter = filter_, event_id]() {
      if (DynamicModuleHttpFilterSharedPtr filter_shared = filter.lock()) {
        filter_shared->onScheduled(event_id);
      }
    });
  }

private:
  // The filter that this scheduler is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the filter.
  DynamicModuleHttpFilterWeakPtr filter_;
  // The dispatcher is used to post the event to the worker thread that filter_ is assigned to.
  Event::Dispatcher& dispatcher_;
};

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
