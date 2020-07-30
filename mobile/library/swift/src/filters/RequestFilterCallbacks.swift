public protocol RequestFilterCallbacks {
  /// Continue iterating through the filter chain with buffered headers and body data.
  ///
  /// This can only be called if the filter has previously returned `stopIteration{...}` from
  /// `onHeaders()`/`onData()`/`onTrailers()`.
  ///
  /// Headers and any buffered body data will be passed to the next filter in the chain.
  ///
  /// If the request is not complete, the filter will still receive `onData()`/`onTrailers()`
  /// calls.
  func continueRequest()
}
