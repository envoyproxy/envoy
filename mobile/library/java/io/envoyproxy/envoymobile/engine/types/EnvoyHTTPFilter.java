package io.envoyproxy.envoymobile.engine.types;

import java.nio.ByteBuffer;
import java.util.concurrent.Executor;
import java.util.List;
import java.util.Map;

public interface EnvoyHTTPFilter {
  /**
   * Called when request headers are sent on the HTTP stream.
   *
   * @param headers,   the headers received.
   * @param endStream, whether the response is headers-only.
   */
  Object[] onRequestHeaders(Map<String, List<String>> headers, boolean endStream);

  /**
   * Called when a request data frame is sent on the HTTP stream. This
   * callback can be invoked multiple times.
   *
   * @param data,      the buffer of the data received.
   * @param endStream, whether the data is the last data frame.
   */
  Object[] onRequestData(ByteBuffer data, boolean endStream);

  /**
   * Called when request trailers are sent on the HTTP stream.
   *
   * @param trailers, the trailers received.
   */
  Object[] onRequestTrailers(Map<String, List<String>> trailers);

  /**
   * Called when response headers are received on the HTTP stream.
   *
   * @param headers,   the headers received.
   * @param endStream, whether the response is headers-only.
   */
  Object[] onResponseHeaders(Map<String, List<String>> headers, boolean endStream);

  /**
   * Called when a data frame is received on the HTTP stream. This
   * callback can be invoked multiple times.
   *
   * @param data,      the buffer of the data received.
   * @param endStream, whether the data is the last data frame.
   */
  Object[] onResponseData(ByteBuffer data, boolean endStream);

  /**
   * Called when response trailers are received on the HTTP stream.
   *
   * @param trailers, the trailers received.
   */
  Object[] onResponseTrailers(Map<String, List<String>> trailers);

  /**
   * Provides asynchronous callbacks to implementations that elect to use them.
   *
   * @param callbacks, thread-safe internal callbacks that enable asynchronous filter interaction.
   */
  void setRequestFilterCallbacks(EnvoyHTTPFilterCallbacks callbacks);

  /**
   * Called when request filter iteration has been asynchronsouly resumed via callback.
   *
   * @param headers,  pending headers that have not yet been forwarded along the filter chain.
   * @param data,     pending data that has not yet been forwarded along the filter chain.
   * @param trailers, pending trailers that have not yet been forwarded along the filter chain.
   */
  Object[] onResumeRequest(Map<String, List<String>> headers, ByteBuffer data,
                           Map<String, List<String>> trailers, boolean endStream);

  /**
   * Provides asynchronous callbacks to implementations that elect to use them.
   *
   * @param callbacks, thread-safe internal callbacks that enable asynchronous filter interaction.
   */
  void setResponseFilterCallbacks(EnvoyHTTPFilterCallbacks callbacks);

  /**
   * Called when response filter iteration has been asynchronsouly resumed via callback.
   *
   * @param headers,  pending headers that have not yet been forwarded along the filter chain.
   * @param data,     pending data that has not yet been forwarded along the filter chain.
   * @param trailers, pending trailers that have not yet been forwarded along the filter chain.
   */
  Object[] onResumeResponse(Map<String, List<String>> headers, ByteBuffer data,
                            Map<String, List<String>> trailers, boolean endStream);

  /**
   * Called when the async HTTP stream has an error.
   *
   * @param errorCode,    the error code.
   * @param message,      the error message.
   * @param attemptCount, the number of times an operation was attempted before firing this error.
   *                      -1 is used in scenarios where it does not make sense to have an attempt
   *                      count for an error. This is different from 0, which intentionally conveys
   *                      that the action was _not_ executed.
   */
  void onError(int errorCode, String message, int attemptCount);

  /**
   * Called when the async HTTP stream is canceled.
   */
  void onCancel();
}
