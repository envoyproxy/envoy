import Foundation

/// RequestFilter supporting asynchronous resumption.
public protocol AsyncRequestFilter: RequestFilter {
  /// Called by the filter manager once to initialize the filter callbacks that the filter should
  /// use.
  ///
  /// - parameter callbacks: The callbacks for this filter to use to interact with the chain.
  func setRequestFilterCallbacks(_ callbacks: RequestFilterCallbacks)

  /// Invoked explicitly in response to an asynchronous `resumeRequest()` callback when filter
  /// iteration has been stopped. The parameters passed to this invocation will be a snapshot
  /// of any stream state that has not yet been forwarded along the filter chain.
  ///
  /// As with other filter invocations, this will be called on Envoy's main thread, and thus
  /// no additional synchronization is required between this and other invocations.
  ///
  /// - param headers: Headers, if `stopIteration` was returned from `onRequestHeaders`.
  /// - param data: Any data that has been buffered where `stopIterationAndBuffer` was returned.
  /// - param trailers: Trailers, if `stopIteration` was returned from `onRequestTrailers`.
  /// - param endStream: True, if the stream ended with the previous (and thus, last) invocation.
  ///
  /// - return: The resumption status including any HTTP entities that will be forwarded.
  func onResumeRequest(
    headers: RequestHeaders?,
    data: Data?,
    trailers: RequestTrailers?,
    endStream: Bool
  ) -> FilterResumeStatus<RequestHeaders, RequestTrailers>
}
