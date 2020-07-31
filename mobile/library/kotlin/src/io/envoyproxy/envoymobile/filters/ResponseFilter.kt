package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

/*
 * Filter executed for inbound responses, providing the ability to observe and mutate streams.
 */
interface ResponseFilter : Filter {
  /**
   * Called by the filter manager once to initialize the filter callbacks that the filter should
   * use.
   *
   * @param callbacks: The callbacks for this filter to use to interact with the chain.
   */
  fun setResponseFilterCallbacks(callbacks: ResponseFilterCallbacks)

  /**
   * Called once when the response is initiated.
   *
   * Filters may mutate or delay the response headers.
   *
   * @param headers:   The current response headers.
   * @param endStream: Whether this is a headers-only response.
   *
   * @return: The header status containing headers with which to continue or buffer.
   */
  fun onResponseHeaders(headers: ResponseHeaders, endStream: Boolean):
    FilterHeadersStatus<ResponseHeaders>

  /**
   * Called any number of times whenever body data is received.
   *
   * Filters may mutate or buffer (defer and concatenate) the data.
   *
   * @param body:      The inbound body data chunk.
   * @param endStream: Whether this is the last data frame.
   *
   * @return: The data status containing body with which to continue or buffer.
   */
  fun onResponseData(body: ByteBuffer, endStream: Boolean): FilterDataStatus

  /**
   * Called at most once when the response is closed from the server with trailers.
   *
   * Filters may mutate or delay the trailers.
   *
   * @param trailers: The inbound trailers.
   *
   * @return: The trailer status containing body with which to continue or buffer.
   */
  fun onResponseTrailers(trailers: ResponseTrailers): FilterTrailersStatus<ResponseTrailers>

  /**
   * Called at most once when an error within Envoy occurs.
   *
   * This should be considered a terminal state, and invalidates any previous attempts to
   * `stopIteration{...}`.
   *
   * @param error: The error that occurred within Envoy.
   */
  fun onError(error: EnvoyError)

  /**
   * Called at most once when the client cancels the stream.
   *
   * This should be considered a terminal state, and invalidates any previous attempts to
   * `stopIteration{...}`.
   */
  fun onCancel()
}
