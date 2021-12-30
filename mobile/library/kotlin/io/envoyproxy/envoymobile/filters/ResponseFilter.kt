package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

/*
 * Filter executed for inbound responses, providing the ability to observe and mutate streams.
 */
interface ResponseFilter : Filter {
  /**
   * Called once when the response is initiated.
   *
   * Filters may mutate or delay the response headers.
   *
   * @param headers:     The current response headers.
   * @param endStream:   Whether this is a headers-only response.
   * @param streamIntel: Internal HTTP stream metrics, context, and other details.
   *
   * @return: The header status containing headers with which to continue or buffer.
   */
  fun onResponseHeaders(headers: ResponseHeaders, endStream: Boolean, streamIntel: StreamIntel):
    FilterHeadersStatus<ResponseHeaders>

  /**
   * Called any number of times whenever body data is received.
   *
   * Filters may mutate or buffer (defer and concatenate) the data.
   *
   * @param body:        The inbound body data chunk.
   * @param endStream:   Whether this is the last data frame.
   * @param streamIntel: Internal HTTP stream metrics, context, and other details.
   *
   * @return: The data status containing body with which to continue or buffer.
   */
  fun onResponseData(body: ByteBuffer, endStream: Boolean, streamIntel: StreamIntel):
    FilterDataStatus<ResponseHeaders>

  /**
   * Called at most once when the response is closed from the server with trailers.
   *
   * Filters may mutate or delay the trailers. Note trailers imply the stream has ended.
   *
   * @param trailers:    The inbound trailers.
   * @param streamIntel: Internal HTTP stream metrics, context, and other details.
   *
   * @return: The trailer status containing body with which to continue or buffer.
   */
  fun onResponseTrailers(trailers: ResponseTrailers, streamIntel: StreamIntel):
    FilterTrailersStatus<ResponseHeaders, ResponseTrailers>

  /**
   * Called at most once when an error within Envoy occurs.
   *
   * Only one of onError, onCancel, or onComplete will be called per stream.
   * This should be considered a terminal state, and invalidates any previous attempts to
   * `stopIteration{...}`.
   *
   * @param error:       The error that occurred within Envoy.
   * @param streamIntel: Internal HTTP stream metrics, context, and other details.
   * @param finalStreamIntel: Final internal HTTP stream metrics, context, and other details.
   */
  fun onError(error: EnvoyError, streamIntel: StreamIntel, finalStreamIntel: FinalStreamIntel)

  /**
   * Called at most once when the client cancels the stream.
   *
   * Only one of onError, onCancel, or onComplete will be called per stream.
   * This should be considered a terminal state, and invalidates any previous attempts to
   * `stopIteration{...}`.
   *
   * @param streamIntel: Internal HTTP stream metrics, context, and other details.
   * @param finalStreamIntel: Final internal HTTP stream metrics, context, and other details.
   */
  fun onCancel(streamIntel: StreamIntel, finalStreamIntel: FinalStreamIntel)

/**
   * Called at most once when the stream completes gracefully.
   *
   * Only one of onError, onCancel, or onComplete will be called per stream.
   * This should be considered a terminal state, and invalidates any previous attempts to
   * `stopIteration{...}`.
   *
   * @param streamIntel: Internal HTTP stream metrics, context, and other details.
   * @param finalStreamIntel: Final internal HTTP stream metrics, context, and other details.
   */
  fun onComplete(streamIntel: StreamIntel, finalStreamIntel: FinalStreamIntel)
}
