#pragma once

#include "envoy/common/optional.h"
#include "envoy/http/message.h"

namespace Http {

/**
 * Supports sending an HTTP request message and receiving a response asynchronously.
 */
class AsyncClient {
public:
  /**
   * Async Client failure reasons.
   */
  enum class FailureReason {
    // The stream has been reset.
    Reset
  };

  /**
   * Notifies caller of async HTTP request status.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() {}

    /**
     * Called when the async HTTP request succeeds.
     * @param response the HTTP response
     */
    virtual void onSuccess(MessagePtr&& response) PURE;

    /**
     * Called when the async HTTP request fails.
     */
    virtual void onFailure(FailureReason reason) PURE;
  };

  /**
   * Notifies caller of async HTTP stream status.
   */
  class StreamCallbacks {
  public:
    virtual ~StreamCallbacks() {}

    /**
     * Called when the async HTTP stream receives headers.
     * @param headers the headers received
     * @param end_stream whether the response is header only
     */
    virtual void onHeaders(HeaderMapPtr&& headers, bool end_stream) PURE;

    /**
     * Called when the async HTTP stream receives data.
     * @param data the data received
     * @param end_stream whether the data is the last data frame
     */
    virtual void onData(Buffer::Instance& data, bool end_stream) PURE;

    /**
     * Called when the async HTTP stream receives trailers.
     * @param trailers the trailers received.
     */
    virtual void onTrailers(HeaderMapPtr&& trailers) PURE;

    /**
     * Called when the async HTTP stream is reset.
     */
    virtual void onResetStream() PURE;
  };

  /**
   * An in-flight HTTP request
   */
  class Request {
  public:
    virtual ~Request() {}

    /**
     * Signals that the request should be cancelled.
     */
    virtual void cancel() PURE;
  };

  /**
   * An in-flight HTTP stream
   */
  class Stream {
  public:
    virtual ~Stream() {}

    /***
     * Send headers to the stream
     * @param headers supplies the headers to send
     * @param end_stream supplies whether this is a header only request
     */
    virtual void sendHeaders(HeaderMap& headers, bool end_stream) PURE;

    /***
     * Send data to the stream
     * @param data supplies the data to send
     * @param end_stream supplies whether this is the last data
     */
    virtual void sendData(Buffer::Instance& data, bool end_stream) PURE;

    /***
     * Send trailers. This implicitly ends the stream.
     * @param trailers supplies the trailers to send
     */
    virtual void sendTrailers(HeaderMap& trailers) PURE;

    /***
     * Close the stream.
     */
    virtual void close() PURE;
  };

  virtual ~AsyncClient() {}

  /**
   * Send an HTTP request asynchronously
   * @param request the request to send.
   * @param callbacks the callbacks to be notified of request status.
   * @return a request handle or nullptr if no request could be created. NOTE: In this case
   *         onFailure() has already been called inline. The client owns the request and the
   *         handle should just be used to cancel.
   */
  virtual Request* send(MessagePtr&& request, Callbacks& callbacks,
                        const Optional<std::chrono::milliseconds>& timeout) PURE;

  virtual Stream* start(StreamCallbacks& callbacks,
                        const Optional<std::chrono::milliseconds>& timeout) PURE;
};

typedef std::unique_ptr<AsyncClient> AsyncClientPtr;

} // Http
