public protocol RequestFilterCallbacks {
  /// Resume iterating through the filter chain with buffered headers and body data.
  ///
  /// This can only be called if the filter has previously returned `stopIteration{...}` from
  /// `onHeaders()`/`onData()`/`onTrailers()`.
  ///
  /// This will result in an `onResumeRequest()` callback on the RequestFilter.
  ///
  /// If the request is not complete, the filter may receive further `onData()`/`onTrailers()`
  /// calls.
  func resumeRequest()
}
