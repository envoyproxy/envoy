#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/ssl/connection.h"
#include "envoy/tracing/http_tracer.h"

namespace Envoy {
namespace Http {

/**
 * Return codes for encode/decode headers filter invocations. The connection manager bases further
 * filter invocations on the return code of the previous filter.
 */
enum class FilterHeadersStatus {
  // Continue filter chain iteration.
  Continue,
  // Do not iterate to any of the remaining filters in the chain. Returning
  // FilterDataStatus::Continue from decodeData()/encodeData() or calling
  // continueDecoding()/continueEncoding() MUST be called if continued filter iteration is desired.
  StopIteration
};

/**
 * Return codes for encode/decode data filter invocations. The connection manager bases further
 * filter invocations on the return code of the previous filter.
 */
enum class FilterDataStatus {
  // Continue filter chain iteration. If headers have not yet been sent to the next filter, they
  // will be sent first via decodeHeaders()/encodeHeaders(). If data has previously been buffered,
  // the data in this callback will be added to the buffer before the entirety is sent to the next
  // filter.
  Continue,
  // Do not iterate to any of the remaining filters in the chain, and buffer body data for later
  // dispatching. Returning FilterDataStatus::Continue from decodeData()/encodeData() or calling
  // continueDecoding()/continueEncoding() MUST be called if continued filter iteration is desired.
  //
  // This should be called by filters which must parse a larger block of the incoming data before
  // continuing processing and so can not push back on streaming data via watermarks.
  //
  // If buffering the request causes buffered data to exceed the configured buffer limit, a 413 will
  // be sent to the user. On the response path exceeding buffer limits will result in a 500.
  StopIterationAndBuffer,
  // Do not iterate to any of the remaining filters in the chain, and buffer body data for later
  // dispatching. Returning FilterDataStatus::Continue from decodeData()/encodeData() or calling
  // continueDecoding()/continueEncoding() MUST be called if continued filter iteration is desired.
  //
  // This will cause the flow of incoming data to cease until one of the continue.*() functions is
  // called.
  //
  // This should be returned by filters which can nominally stream data but have a transient back-up
  // such as the configured delay of the fault filter, or if the router filter is still fetching an
  // upstream connection.
  StopIterationAndWatermark,
  // Do not iterate to any of the remaining filters in the chain, but do not buffer any of the
  // body data for later dispatching. Returning FilterDataStatus::Continue from
  // decodeData()/encodeData() or calling continueDecoding()/continueEncoding() MUST be called if
  // continued filter iteration is desired.
  StopIterationNoBuffer
};

/**
 * Return codes for encode/decode trailers filter invocations. The connection manager bases further
 * filter invocations on the return code of the previous filter.
 */
enum class FilterTrailersStatus {
  // Continue filter chain iteration.
  Continue,
  // Do not iterate to any of the remaining filters in the chain. Calling
  // continueDecoding()/continueEncoding() MUST be called if continued filter iteration is desired.
  StopIteration
};

/**
 * The stream filter callbacks are passed to all filters to use for writing response data and
 * interacting with the underlying stream in general.
 */
class StreamFilterCallbacks {
public:
  virtual ~StreamFilterCallbacks() {}

  /**
   * @return const Network::Connection* the originating connection, or nullptr if there is none.
   */
  virtual const Network::Connection* connection() PURE;

  /**
   * @return Event::Dispatcher& the thread local dispatcher for allocating timers, etc.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Reset the underlying stream.
   */
  virtual void resetStream() PURE;

  /**
   * Returns the route for the current request. The assumption is that the implementation can do
   * caching where applicable to avoid multiple lookups. If a filter has modified the headers in
   * a way that affects routing, clearRouteCache() must be called to clear the cache.
   *
   * NOTE: In the future we may want to allow the filter to override the route entry.
   */
  virtual Router::RouteConstSharedPtr route() PURE;

  /**
   * Clears the route cache for the current request. This must be called when a filter has modified
   * the headers in a way that would affect routing.
   */
  virtual void clearRouteCache() PURE;

  /**
   * @return uint64_t the ID of the originating stream for logging purposes.
   */
  virtual uint64_t streamId() PURE;

  /**
   * @return requestInfo for logging purposes. Individual filter may add specific information to be
   * put into the access log.
   */
  virtual RequestInfo::RequestInfo& requestInfo() PURE;

  /**
   * @return span context used for tracing purposes. Individual filters may add or modify
   *              information in the span context.
   */
  virtual Tracing::Span& activeSpan() PURE;

  /**
   * @return tracing configuration.
   */
  virtual const Tracing::Config& tracingConfig() PURE;
};

/**
 * Stream decoder filter callbacks add additional callbacks that allow a decoding filter to restart
 * decoding if they decide to hold data (e.g. for buffering or rate limiting).
 */
class StreamDecoderFilterCallbacks : public virtual StreamFilterCallbacks {
public:
  /**
   * Continue iterating through the filter chain with buffered headers and body data. This routine
   * can only be called if the filter has previously returned StopIteration from decodeHeaders()
   * AND either StopIterationAndBuffer or StopIterationNoBuffer from each previous call to
   * decodeData(). The connection manager will dispatch headers and any buffered body data to the
   * next filter in the chain. Further note that if the request is not complete, this filter will
   * still receive decodeData() calls and must return an appropriate status code depending on what
   * the filter needs to do.
   */
  virtual void continueDecoding() PURE;

  /**
   * @return const Buffer::Instance* the currently buffered data as buffered by this filter or
   *         previous ones in the filter chain. May be nullptr if nothing has been buffered yet.
   */
  virtual const Buffer::Instance* decodingBuffer() PURE;

  /**
   * Add buffered body data. This method is used in advanced cases where returning
   * StopIterationAndBuffer from decodeData() is not sufficient.
   *
   * 1) If a headers only request needs to be turned into a request with a body, this method can
   * be called to add body in the decodeHeaders() callback. Subsequent filters will receive
   * decodeHeaders(..., false) followed by decodeData(..., true). This works both in the direct
   * iteration as well as the continuation case.
   *
   * 2) If a filter is going to look at all buffered data from within a data callback with end
   * stream set, this method can be called to immediately buffer the data. This avoids having
   * to deal with the existing buffered data and the data from the current callback.
   *
   * 3) If additional buffered body data needs to be added by a filter before continuation of data
   * to further filters (outside of callback context).
   *
   * 4) If additional data needs to be added in the decodeTrailers() callback, this method can be
   * called in the context of the callback. All further filters will receive decodeData(..., false)
   * followed by decodeTrailers().
   *
   * It is an error to call this method in any other case.
   *
   * @param data Buffer::Instance supplies the data to be decoded.
   * @param streaming_filter boolean supplies if this filter streams data or buffers the full body.
   */
  virtual void addDecodedData(Buffer::Instance& data, bool streaming_filter) PURE;

  /**
   * Called with headers to be encoded, optionally indicating end of stream.
   *
   * The connection manager inspects certain pseudo headers that are not actually sent downstream.
   * - See source/common/http/headers.h
   *
   * @param headers supplies the headers to be encoded.
   * @param end_stream supplies whether this is a header only request/response.
   */
  virtual void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) PURE;

  /**
   * Called with data to be encoded, optionally indicating end of stream.
   * @param data supplies the data to be encoded.
   * @param end_stream supplies whether this is the last data frame.
   */
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called with trailers to be encoded. This implicitly ends the stream.
   * @param trailers supplies the trailers to encode.
   */
  virtual void encodeTrailers(HeaderMapPtr&& trailers) PURE;

  /**
   * Called when the buffer for a decoder filter or any buffers the filter sends data to go over
   * their high watermark.
   *
   * In the case of a filter such as the router filter, which spills into multiple buffers (codec,
   * connection etc.) this may be called multiple times. Any such filter is responsible for calling
   * the low watermark callbacks an equal number of times as the respective buffers are drained.
   */
  virtual void onDecoderFilterAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when a decoder filter or any buffers the filter sends data to go from over its high
   * watermark to under its low watermark.
   */
  virtual void onDecoderFilterBelowWriteBufferLowWatermark() PURE;

  /**
   * This routine can be called by a filter to subscribe to watermark events on the downstream
   * stream and downstream connection.
   *
   * Immediately after subscribing, the filter will get a high watermark callback for each
   * outstanding backed up buffer.
   */
  virtual void addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& callbacks) PURE;

  /**
   * This routine can be called by a filter to stop subscribing to watermark events on the
   * downstream stream and downstream connection.
   *
   * It is not safe to call this from under the stack of a DownstreamWatermarkCallbacks callback.
   */
  virtual void removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& callbacks) PURE;

  /**
   * This routine may be called to change the buffer limit for decoder filters.
   *
   * @param boolean supplies the desired buffer limit.
   */
  virtual void setDecoderBufferLimit(uint32_t limit) PURE;

  /**
   * This routine returns the current buffer limit for decoder filters. Filters should abide by
   * this limit or change it via setDecoderBufferLimit.
   * A buffer limit of 0 bytes indicates no limits are applied.
   *
   * @return the buffer limit the filter should apply.
   */
  virtual uint32_t decoderBufferLimit() PURE;
};

/**
 * Common base class for both decoder and encoder filters.
 */
class StreamFilterBase {
public:
  virtual ~StreamFilterBase() {}

  /**
   * This routine is called prior to a filter being destroyed. This may happen after normal stream
   * finish (both downstream and upstream) or due to reset. Every filter is responsible for making
   * sure that any async events are cleaned up in the context of this routine. This includes timers,
   * network calls, etc. The reason there is an onDestroy() method vs. doing this type of cleanup
   * in the destructor is due to the deferred deletion model that Envoy uses to avoid stack unwind
   * complications. Filters must not invoke either encoder or decoder filter callbacks after having
   * onDestroy() invoked.
   */
  virtual void onDestroy() PURE;
};

/**
 * Stream decoder filter interface.
 */
class StreamDecoderFilter : public StreamFilterBase {
public:
  /**
   * Called with decoded headers, optionally indicating end of stream.
   * @param headers supplies the decoded headers map.
   * @param end_stream supplies whether this is a header only request/response.
   * @return FilterHeadersStatus determines how filter chain iteration proceeds.
   */
  virtual FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) PURE;

  /**
   * Called with a decoded data frame.
   * @param data supplies the decoded data.
   * @param end_stream supplies whether this is the last data frame.
   * @return FilterDataStatus determines how filter chain iteration proceeds.
   */
  virtual FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called with decoded trailers, implicitly ending the stream.
   * @param trailers supplies the decoded trailers.
   */
  virtual FilterTrailersStatus decodeTrailers(HeaderMap& trailers) PURE;

  /**
   * Called by the filter manager once to initialize the filter decoder callbacks that the
   * filter should use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) PURE;
};

typedef std::shared_ptr<StreamDecoderFilter> StreamDecoderFilterSharedPtr;

/**
 * Stream encoder filter callbacks add additional callbacks that allow a encoding filter to restart
 * encoding if they decide to hold data (e.g. for buffering or rate limiting).
 */
class StreamEncoderFilterCallbacks : public virtual StreamFilterCallbacks {
public:
  /**
   * Continue iterating through the filter chain with buffered headers and body data. This routine
   * can only be called if the filter has previously returned StopIteration from encodeHeaders() AND
   * either StopIterationAndBuffer or StopIterationNoBuffer from each previous call to encodeData().
   * The connection manager will dispatch headers and any buffered body data to the next filter in
   * the chain. Further note that if the response is not complete, this filter will still receive
   * encodeData() calls and must return an appropriate status code depending on what the filter
   * needs to do.
   */
  virtual void continueEncoding() PURE;

  /**
   * @return const Buffer::Instance* the currently buffered data as buffered by this filter or
   *         previous ones in the filter chain. May be nullptr if nothing has been buffered yet.
   */
  virtual const Buffer::Instance* encodingBuffer() PURE;

  /**
   * Add buffered body data. This method is used in advanced cases where returning
   * StopIterationAndBuffer from encodeData() is not sufficient.
   *
   * 1) If a headers only response needs to be turned into a response with a body, this method can
   * be called to add body in the encodeHeaders() callback. Subsequent filters will receive
   * encodeHeaders(..., false) followed by encodeData(..., true). This works both in the direct
   * iteration as well as the continuation case.
   *
   * 2) If a filter is going to look at all buffered data from within a data callback with end
   * stream set, this method can be called to immediately buffer the data. This avoids having
   * to deal with the existing buffered data and the data from the current callback.
   *
   * 3) If additional buffered body data needs to be added by a filter before continuation of data
   * to further filters (outside of callback context).
   *
   * 4) If additional data needs to be added in the encodeTrailers() callback, this method can be
   * called in the context of the callback. All further filters will receive encodeData(..., false)
   * followed by encodeTrailers().
   *
   * It is an error to call this method in any other case.
   *
   * @param data Buffer::Instance supplies the data to be encoded.
   * @param streaming_filter boolean supplies if this filter streams data or buffers the full body.
   */
  virtual void addEncodedData(Buffer::Instance& data, bool streaming_filter) PURE;

  /**
   * Called when an encoder filter goes over its high watermark.
   */
  virtual void onEncoderFilterAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when a encoder filter goes from over its high watermark to under its low watermark.
   */
  virtual void onEncoderFilterBelowWriteBufferLowWatermark() PURE;

  /**
   * This routine may be called to change the buffer limit for encoder filters.
   *
   * @limit settings supplies the desired buffer limit.
   */
  virtual void setEncoderBufferLimit(uint32_t limit) PURE;

  /**
   * This routine returns the current buffer limit for encoder filters. Filters should abide by
   * this limit or change it via setEncoderBufferLimit.
   * A buffer limit of 0 bytes indicates no limits are applied.
   *
   * @return the buffer limit the filter should apply.
   */
  virtual uint32_t encoderBufferLimit() PURE;
};

/**
 * Stream encoder filter interface.
 */
class StreamEncoderFilter : public StreamFilterBase {
public:
  /**
   * Called with headers to be encoded, optionally indicating end of stream.
   * @param headers supplies the headers to be encoded.
   * @param end_stream supplies whether this is a header only request/response.
   * @return FilterHeadersStatus determines how filter chain iteration proceeds.
   */
  virtual FilterHeadersStatus encodeHeaders(HeaderMap& headers, bool end_stream) PURE;

  /**
   * Called with data to be encoded, optionally indicating end of stream.
   * @param data supplies the data to be encoded.
   * @param end_stream supplies whether this is the last data frame.
   * @return FilterDataStatus determines how filter chain iteration proceeds.
   */
  virtual FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called with trailers to be encoded, implicitly ending the stream.
   * @param trailers supplies the trailes to be encoded.
   */
  virtual FilterTrailersStatus encodeTrailers(HeaderMap& trailers) PURE;

  /**
   * Called by the filter manager once to initialize the filter callbacks that the filter should
   * use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) PURE;
};

typedef std::shared_ptr<StreamEncoderFilter> StreamEncoderFilterSharedPtr;

/**
 * A filter that handles both encoding and decoding.
 */
class StreamFilter : public StreamDecoderFilter, public StreamEncoderFilter {};

typedef std::shared_ptr<StreamFilter> StreamFilterSharedPtr;

/**
 * These callbacks are provided by the connection manager to the factory so that the factory can
 * build the filter chain in an application specific way.
 */
class FilterChainFactoryCallbacks {
public:
  virtual ~FilterChainFactoryCallbacks() {}

  /**
   * Add a decoder filter that is used when reading stream data.
   * @param filter supplies the filter to add.
   */
  virtual void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) PURE;

  /**
   * Add an encoder filter that is used when writing stream data.
   * @param filter supplies the filter to add.
   */
  virtual void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) PURE;

  /**
   * Add a decoder/encoder filter that is used both when reading and writing stream data.
   * @param filter supplies the filter to add.
   */
  virtual void addStreamFilter(Http::StreamFilterSharedPtr filter) PURE;

  /**
   * Add an access log handler that is called when the stream is destroyed.
   * @param handler supplies the handler to add.
   */
  virtual void addAccessLogHandler(AccessLog::InstanceSharedPtr handler) PURE;
};

/**
 * A FilterChainFactory is used by a connection manager to create an HTTP level filter chain when a
 * new stream is created on the connection (either locally or remotely). Typically it would be
 * implemented by a configuration engine that would install a set of filters that are able to
 * process an application scenario on top of a stream.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() {}

  /**
   * Called when a new stream is created on the connection.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  FilterChainFactoryCallbacks.
   */
  virtual void createFilterChain(FilterChainFactoryCallbacks& callbacks) PURE;
};

} // namespace Http
} // namespace Envoy
