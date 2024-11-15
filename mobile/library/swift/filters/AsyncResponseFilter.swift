import Foundation

/// ResponseFilter supporting asynchronous resumption.
public protocol AsyncResponseFilter: ResponseFilter {
  /// Called by the filter manager once to initialize the filter callbacks that the filter should
  /// use.
  ///
  /// - parameter callbacks: The callbacks for this filter to use to interact with the chain.
  func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks)

  /// Invoked explicitly in response to an asynchronous `resumeResponse()` callback when filter
  /// iteration has been stopped. The parameters passed to this invocation will be a snapshot
  /// of any stream state that has not yet been forwarded along the filter chain.
  ///
  /// As with other filter invocations, this will be called on Envoy's main thread, and thus
  /// no additional synchronization is required between this and other invocations.
  ///
  /// - parameter headers:     Headers, if `stopIteration` was returned from `onResponseHeaders`.
  /// - parameter data:        Any data that has been buffered where `stopIterationAndBuffer` was
  ///                          returned.
  /// - parameter trailers:    Trailers, if `stopIteration` was returned from `onReponseTrailers`.
  /// - parameter endStream:   True, if the stream ended with the previous (and thus, last)
  ///                          invocation.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The resumption status including any HTTP entities that will be forwarded.
  func onResumeResponse(
    headers: ResponseHeaders?,
    data: Data?,
    trailers: ResponseTrailers?,
    endStream: Bool,
    streamIntel: StreamIntel
  ) -> FilterResumeStatus<ResponseHeaders, ResponseTrailers>
}
