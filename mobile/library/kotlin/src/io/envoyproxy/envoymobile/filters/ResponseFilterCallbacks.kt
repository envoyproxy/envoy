package io.envoyproxy.envoymobile

interface ResponseFilterCallbacks {
  /**
   * Resume iterating through the filter chain with buffered headers and body data.
   *
   * This can only be called if the filter has previously returned `stopIteration{...}` from
   * `onHeaders()`/`onData()`/`onTrailers()`.
   *
   * This will result in an `onResumeResponse()` callback on the ResponseFilter.
   *
   * If the response is not complete, the filter may receive further `onData()`/`onTrailers()`
   * calls.
   */
  fun resumeResponse()
}
