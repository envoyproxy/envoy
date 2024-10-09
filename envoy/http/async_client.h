#pragma once

#include <chrono>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {
class FilterConfig;
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;
} // namespace Router
namespace Http {

/**
 * Callbacks for sidestream connection (from http async client) watermark limits.
 */
class SidestreamWatermarkCallbacks {
public:
  virtual ~SidestreamWatermarkCallbacks() = default;

  /**
   * Called when the sidestream connection or stream goes over its high watermark. Note that this
   * may be called separately for both the stream going over and the connection going over. It
   * is the responsibility of the sidestreamWatermarkCallbacks implementation to handle unwinding
   * multiple high and low watermark calls.
   */
  virtual void onSidestreamAboveHighWatermark() PURE;

  /**
   * Called when the sidestream connection or stream goes from over its high watermark to under its
   * low watermark. As with onSidestreamAboveHighWatermark above, this may be called independently
   * when both the stream and the connection go under the low watermark limit, and the callee must
   * ensure that the flow of data does not resume until all callers which were above their high
   * watermarks have gone below.
   */
  virtual void onSidestreamBelowLowWatermark() PURE;
};

/**
 * Supports sending an HTTP request message and receiving a response asynchronously.
 */
class AsyncClient {
public:
  /**
   * An in-flight HTTP request.
   */
  class Request {
  public:
    virtual ~Request() = default;

    /**
     * Signals that the request should be cancelled.
     */
    virtual void cancel() PURE;
  };

  /**
   * Async Client failure reasons.
   */
  enum class FailureReason {
    // The stream has been reset.
    Reset,
    // The stream exceeds the response buffer limit.
    ExceedResponseBufferLimit
  };

  /**
   * Notifies caller of async HTTP request status.
   *
   * To support a use case where a caller makes multiple requests in parallel,
   * individual callback methods provide request context corresponding to that response.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    /**
     * Called when the async HTTP request succeeds.
     * @param request  request handle.
     *                 NOTE: request handle is passed for correlation purposes only, e.g.
     *                 for client code to be able to exclude that handle from a list of
     *                 requests in progress.
     * @param response the HTTP response
     */
    virtual void onSuccess(const Request& request, ResponseMessagePtr&& response) PURE;

    /**
     * Called when the async HTTP request fails.
     * @param request request handle.
     *                NOTE: request handle is passed for correlation purposes only, e.g.
     *                for client code to be able to exclude that handle from a list of
     *                requests in progress.
     * @param reason  failure reason
     */
    virtual void onFailure(const Request& request, FailureReason reason) PURE;

    /**
     * Called before finalizing upstream span when the request is complete or reset.
     * @param span a tracing span to fill with extra tags.
     * @param response_headers the response headers.
     */
    virtual void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span& span,
                                              const Http::ResponseHeaderMap* response_headers) PURE;
  };

  /**
   * Notifies caller of async HTTP stream status.
   * Note the HTTP stream is full-duplex, even if the local to remote stream has been ended
   * by Stream.sendHeaders/sendData with end_stream=true or sendTrailers,
   * StreamCallbacks can continue to receive events until the remote to local stream is closed,
   * and vice versa.
   */
  class StreamCallbacks {
  public:
    virtual ~StreamCallbacks() = default;

    /**
     * Called when all headers get received on the async HTTP stream.
     * @param headers the headers received
     * @param end_stream whether the response is header only
     */
    virtual void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) PURE;

    /**
     * Called when a data frame get received on the async HTTP stream.
     * This can be invoked multiple times if the data get streamed.
     * @param data the data received
     * @param end_stream whether the data is the last data frame
     */
    virtual void onData(Buffer::Instance& data, bool end_stream) PURE;

    /**
     * Called when all trailers get received on the async HTTP stream.
     * @param trailers the trailers received.
     */
    virtual void onTrailers(ResponseTrailerMapPtr&& trailers) PURE;

    /**
     * Called when both the local and remote have gracefully closed the stream.
     * Useful for asymmetric cases where end_stream may not be bidirectionally observable.
     * Note this is NOT called on stream reset.
     */
    virtual void onComplete() PURE;

    /**
     * Called when the async HTTP stream is reset.
     */
    virtual void onReset() PURE;
  };

  using StreamDestructorCallbacks = std::function<void()>;

  /**
   * An in-flight HTTP stream.
   */
  class Stream {
  public:
    virtual ~Stream() = default;

    /***
     * Send headers to the stream. This method cannot be invoked more than once and
     * need to be called before sendData.
     * @param headers supplies the headers to send.
     * @param end_stream supplies whether this is a header only request.
     */
    virtual void sendHeaders(RequestHeaderMap& headers, bool end_stream) PURE;

    /***
     * Send data to the stream. This method can be invoked multiple times if it get streamed.
     * To end the stream without data, call this method with empty buffer.
     * @param data supplies the data to send.
     * @param end_stream supplies whether this is the last data.
     */
    virtual void sendData(Buffer::Instance& data, bool end_stream) PURE;

    /***
     * Send trailers. This method cannot be invoked more than once, and implicitly ends the stream.
     * @param trailers supplies the trailers to send.
     */
    virtual void sendTrailers(RequestTrailerMap& trailers) PURE;

    /***
     * Reset the stream.
     */
    virtual void reset() PURE;

    /***
     * Register callback to be called on stream destruction. This callback must persist beyond the
     * lifetime of the stream or be unregistered via removeDestructorCallback. If there's already a
     * destructor callback registered, this method will ASSERT-fail.
     */
    virtual void setDestructorCallback(StreamDestructorCallbacks callback) PURE;

    /***
     * Remove previously set destructor callback. Calling this without having previously set a
     * Destructor callback will ASSERT-fail.
     */
    virtual void removeDestructorCallback() PURE;

    /***
     * Register a callback to be called when high/low write buffer watermark events occur on the
     * stream. This callback must persist beyond the lifetime of the stream or be unregistered via
     * removeWatermarkCallbacks. If there's already a watermark callback registered, this method
     * will trigger ENVOY_BUG.
     */
    virtual void setWatermarkCallbacks(Http::SidestreamWatermarkCallbacks& callbacks) PURE;

    /***
     * Remove previously set watermark callbacks. If there's no watermark callback registered, this
     * method will trigger ENVOY_BUG.
     */
    virtual void removeWatermarkCallbacks() PURE;

    /***
     * @returns if the stream has enough buffered outbound data to be over the configured buffer
     * limits
     */
    virtual bool isAboveWriteBufferHighWatermark() const PURE;

    /***
     * @returns the stream info object associated with the stream.
     */
    virtual const StreamInfo::StreamInfo& streamInfo() const PURE;
    virtual StreamInfo::StreamInfo& streamInfo() PURE;
  };

  /***
   * An in-flight HTTP request to which additional data and trailers can be sent, as well as resets
   * and other stream-cancelling events. Must be terminated by sending trailers or data with
   * end_stream.
   */
  class OngoingRequest : public virtual Request, public virtual Stream {
  public:
    /***
     * Takes ownership of trailers, and sends it to the underlying stream.
     * @param trailers owned trailers to pass to upstream.
     */
    virtual void captureAndSendTrailers(RequestTrailerMapPtr&& trailers) PURE;
  };

  virtual ~AsyncClient() = default;

  /**
   * A context from the caller of an async client.
   */
  struct ParentContext {
    const StreamInfo::StreamInfo* stream_info{nullptr};
  };

  /**
   * A structure to hold the options for AsyncStream object.
   */
  struct StreamOptions {
    StreamOptions& setTimeout(const absl::optional<std::chrono::milliseconds>& v) {
      timeout = v;
      return *this;
    }
    StreamOptions& setTimeout(const std::chrono::milliseconds& v) {
      timeout = v;
      return *this;
    }
    StreamOptions& setBufferBodyForRetry(bool v) {
      buffer_body_for_retry = v;
      return *this;
    }
    StreamOptions& setSendXff(bool v) {
      send_xff = v;
      return *this;
    }
    StreamOptions& setSendInternal(bool v) {
      send_internal = v;
      return *this;
    }
    StreamOptions& setHashPolicy(
        const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>& v) {
      hash_policy = v;
      return *this;
    }
    StreamOptions& setParentContext(const ParentContext& v) {
      parent_context = v;
      return *this;
    }
    // Set dynamic metadata of async stream. If a metadata record with filter name 'envoy.lb' is
    // provided, metadata match criteria of async stream route will be overridden by the metadata
    // and then used by the subset load balancer.
    StreamOptions& setMetadata(const envoy::config::core::v3::Metadata& m) {
      metadata = m;
      return *this;
    }

    // Set FilterState on async stream allowing upstream filters to access it.
    StreamOptions& setFilterState(Envoy::StreamInfo::FilterStateSharedPtr fs) {
      filter_state = fs;
      return *this;
    }

    // Set buffer restriction and accounting for the stream.
    StreamOptions& setBufferAccount(const Buffer::BufferMemoryAccountSharedPtr& account) {
      account_ = account;
      return *this;
    }
    StreamOptions& setBufferLimit(uint32_t limit) {
      buffer_limit_ = limit;
      return *this;
    }

    // this should be done with setBufferedBodyForRetry=true ?
    // The retry policy can be set as either a proto or Router::RetryPolicy but
    // not both. If both formats of the options are set, the more recent call
    // will overwrite the older one.
    StreamOptions& setRetryPolicy(const envoy::config::route::v3::RetryPolicy& p) {
      retry_policy = p;
      parsed_retry_policy = nullptr;
      return *this;
    }

    // The retry policy can be set as either a proto or Router::RetryPolicy but
    // not both. If both formats of the options are set, the more recent call
    // will overwrite the older one.
    StreamOptions& setRetryPolicy(const Router::RetryPolicy& p) {
      parsed_retry_policy = &p;
      retry_policy = absl::nullopt;
      return *this;
    }
    StreamOptions& setFilterConfig(const Router::FilterConfigSharedPtr& config) {
      filter_config_ = config;
      return *this;
    }

    StreamOptions& setIsShadow(bool s) {
      is_shadow = s;
      return *this;
    }

    StreamOptions& setDiscardResponseBody(bool discard) {
      discard_response_body = discard;
      return *this;
    }

    StreamOptions& setIsShadowSuffixDisabled(bool d) {
      is_shadow_suffixed_disabled = d;
      return *this;
    }

    StreamOptions& setParentSpan(Tracing::Span& parent_span) {
      parent_span_ = &parent_span;
      return *this;
    }
    StreamOptions& setChildSpanName(const std::string& child_span_name) {
      child_span_name_ = child_span_name;
      return *this;
    }
    StreamOptions& setSampled(absl::optional<bool> sampled) {
      sampled_ = sampled;
      return *this;
    }

    // For gmock test
    bool operator==(const StreamOptions& src) const {
      return timeout == src.timeout && buffer_body_for_retry == src.buffer_body_for_retry &&
             send_xff == src.send_xff && send_internal == src.send_internal &&
             parent_span_ == src.parent_span_ && child_span_name_ == src.child_span_name_ &&
             sampled_ == src.sampled_;
    }

    // The timeout supplies the stream timeout, measured since when the frame with
    // end_stream flag is sent until when the first frame is received.
    absl::optional<std::chrono::milliseconds> timeout;

    // The buffer_body_for_retry specifies whether the streamed body will be buffered so that
    // it can be retried. In general, this should be set to false for a true stream. However,
    // streaming is also used in certain cases such as gRPC unary calls, where retry can
    // still be useful.
    bool buffer_body_for_retry{false};

    // If true, x-forwarded-for header will be added.
    bool send_xff{true};

    // If true, x-envoy-internal header will be added.
    bool send_internal{true};

    // Provides the hash policy for hashing load balancing strategies.
    Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy> hash_policy;

    // Provides parent context. Currently, this holds stream info from the caller.
    ParentContext parent_context;

    envoy::config::core::v3::Metadata metadata;
    Envoy::StreamInfo::FilterStateSharedPtr filter_state;

    // Buffer memory account for tracking bytes.
    Buffer::BufferMemoryAccountSharedPtr account_{nullptr};

    absl::optional<uint32_t> buffer_limit_;

    absl::optional<envoy::config::route::v3::RetryPolicy> retry_policy;
    const Router::RetryPolicy* parsed_retry_policy{nullptr};

    Router::FilterConfigSharedPtr filter_config_;

    bool is_shadow{false};

    bool is_shadow_suffixed_disabled{false};
    bool discard_response_body{false};

    // The parent span that child spans are created under to trace egress requests/responses.
    // If not set, requests will not be traced.
    Tracing::Span* parent_span_{nullptr};
    // The name to give to the child span that represents the async http request.
    // If left empty and parent_span_ is set, then the default name will have the cluster name.
    // Only used if parent_span_ is set.
    std::string child_span_name_{""};
    // Sampling decision for the tracing span. The span is sampled by default.
    absl::optional<bool> sampled_{true};
  };

  /**
   * A structure to hold the options for AsyncRequest object.
   */
  using RequestOptions = StreamOptions;

  /**
   * Send an HTTP request asynchronously
   * @param request the request to send.
   * @param callbacks the callbacks to be notified of request status.
   * @param options the data struct to control the request sending.
   * @return a request handle or nullptr if no request could be created. NOTE: In this case
   *         onFailure() has already been called inline. The client owns the request and the
   *         handle should just be used to cancel.
   */

  virtual Request* send(RequestMessagePtr&& request, Callbacks& callbacks,
                        const RequestOptions& options) PURE;

  /**
   * Starts a new OngoingRequest asynchronously with the given headers.
   *
   * @param request_headers headers to send.
   * @param callbacks the callbacks to be notified of request status.
   * @param options the data struct to control the request sending.
   * @return a request handle or nullptr if no request could be created. See note attached to
   * `send`. Calling startRequest will not trigger end stream. For header-only requests, `send`
   * should be called instead.
   */
  virtual OngoingRequest* startRequest(RequestHeaderMapPtr&& request_headers, Callbacks& callbacks,
                                       const RequestOptions& options) PURE;

  /**
   * Start an HTTP stream asynchronously, without an associated HTTP request.
   * @param callbacks the callbacks to be notified of stream status.
   * @param options the data struct to control the stream.
   * @return a stream handle or nullptr if no stream could be started. NOTE: In this case
   *         onResetStream() has already been called inline. The client owns the stream and
   *         the handle can be used to send more messages or close the stream.
   */
  virtual Stream* start(StreamCallbacks& callbacks, const StreamOptions& options) PURE;

  /**
   * @return Event::Dispatcher& the dispatcher backing this client.
   */
  virtual Event::Dispatcher& dispatcher() PURE;
};

using AsyncClientPtr = std::unique_ptr<AsyncClient>;

} // namespace Http
} // namespace Envoy
