#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/http/access_log.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/ssl/connection.h"

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
  StopIterationAndBuffer,
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
   * Register a callback that will get called when the underlying stream has been reset (either
   * upstream reset from another filter or downstream reset from the remote side).
   */
  virtual void addResetStreamCallback(std::function<void()> callback) PURE;

  /**
   * @return uint64_t the ID of the originating connection for logging purposes.
   */
  virtual uint64_t connectionId() PURE;

  /**
   * @return Ssl::Connection* the ssl connection.
   */
  virtual Ssl::Connection* ssl() PURE;

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
   * caching where applicable to avoid multiple lookups.
   *
   * NOTE: This breaks down a bit if the caller knows it has modified something that would affect
   *       routing (such as the request headers). In practice, we don't do this anywhere currently,
   *       but in the future we might need to provide the ability to clear the cache if a filter
   *       knows that it has modified the headers in a way that would affect routing. In the future
   *       we may also want to allow the filter to override the route entry.
   */
  virtual Router::RouteConstSharedPtr route() PURE;

  /**
   * @return uint64_t the ID of the originating stream for logging purposes.
   */
  virtual uint64_t streamId() PURE;

  /**
   * @return requestInfo for logging purposes. Individual filter may add specific information to be
   * put into the access log.
   */
  virtual AccessLog::RequestInfo& requestInfo() PURE;

  /**
   * @return the trusted downstream address for the connection.
   */
  virtual const std::string& downstreamAddress() PURE;
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
   * @return Buffer::InstancePtr& the currently buffered data as buffered by this filter or previous
   *         ones in the filter chain. May be nullptr if nothing has been buffered yet. Callers
   *         are free to remove, reallocate, and generally modify the buffered data.
   *
   *         NOTE: For common buffering cases, there is no need for each filter to manually handle
   *         buffering. If decodeData() returns StopIterationAndBuffer, the filter manager will
   *         buffer the data passed to the callback on behalf of the filter.
   *
   *         NOTE: In complex cases, the filter may wish to manually modify the buffer. One example
   *         of this is switching a header only request to a request with body data. If a filter
   *         receives decodeHeaders(..., true), it has the option of filling decodingBuffer() with
   *         body data. Subsequent filters will receive decodeHeaders(..., false) followed by
   *         decodeData(..., true). This works both in the direct iteration as well as the
   *         continuation case.
   */
  virtual Buffer::InstancePtr& decodingBuffer() PURE;

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
};

/**
 * Stream decoder filter interface.
 */
class StreamDecoderFilter {
public:
  virtual ~StreamDecoderFilter() {}

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
   * filter should use.
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
   * @return Buffer::InstancePtr& the currently buffered data as buffered by this filter or previous
   *         ones in the filter chain. May be nullptr if nothing has been buffered yet. Callers
   *         are free to remove, reallocate, and generally modify the buffered data.
   *
   *         NOTE: For common buffering cases, there is no need for each filter to manually handle
   *         buffering. If encodeData() returns StopIterationAndBuffer, the filter manager will
   *         buffer the data passed to the callback on behalf of the filter.
   *
   *         NOTE: In complex cases, the filter may wish to manually modify the buffer. One example
   *         of this is switching a header only request to a request with body data. If a filter
   *         receives encodeHeaders(..., true), it has the option of filling encodingBuffer() with
   *         body data. Subsequent filters will receive encodeHeaders(..., false) followed by
   *         encodeData(..., true). This works both in the direct iteration as well as the
   *         continuation case.
   */
  virtual Buffer::InstancePtr& encodingBuffer() PURE;
};

/**
 * Stream encoder filter interface.
 */
class StreamEncoderFilter {
public:
  virtual ~StreamEncoderFilter() {}

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
   * use.
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
  virtual void addAccessLogHandler(Http::AccessLog::InstanceSharedPtr handler) PURE;
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

} // Http
