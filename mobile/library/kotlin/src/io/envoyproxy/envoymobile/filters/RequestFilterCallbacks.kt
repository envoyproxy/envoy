package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

interface RequestFilterCallbacks {
  /**
   * Continue iterating through the filter chain with buffered headers and body data.
   *
   * This can only be called if the filter has previously returned `stopIteration{...}` from
   * `onHeaders()`/`onData()`/`onTrailers()`.
   *
   * Headers and any buffered body data will be passed to the next filter in the chain.
   *
   * If the request is not complete, the filter will still receive `onData()`/`onTrailers()`
   * calls.
   */
  fun continueRequest()

  /**
   * @return ByteBuffer, The currently buffered data as buffered by the filter or previous ones in
   *                     the filter chain. Nil if nothing has been buffered yet.
   */
  fun requestBuffer(): ByteBuffer?

  /**
   * Adds request trailers. May only be called in `onHeaders()`/`onData()` when
   * `endStream = true` in order to guarantee that the client will not send its own trailers.
   *
   * @param trailers: The trailers to add and pass to subsequent filters.
   */
  fun addRequestTrailers(trailers: RequestTrailers)
}
