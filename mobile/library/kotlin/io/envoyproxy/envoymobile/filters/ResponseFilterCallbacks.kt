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

  /**
   * Reset the underlying stream idle timeout to its configured threshold.
   *
   * This may be useful if a filter stops iteration for an extended period of time, since ordinarily
   * timeouts will still apply. This may be called periodically to continue to indicate "activity"
   * on the stream.
   */
  fun resetIdleTimer()
}
