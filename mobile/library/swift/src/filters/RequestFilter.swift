import Foundation

/// Filter executed for outbound requests, providing the ability to observe and mutate streams.
public protocol RequestFilter: Filter {
  /// Called by the filter manager once to initialize the filter callbacks that the filter should
  /// use.
  ///
  /// - parameter callbacks: The callbacks for this filter to use to interact with the chain.
  func setRequestFilterCallbacks(_ callbacks: RequestFilterCallbacks)

  /// Called once when the request is initiated.
  ///
  /// Filters may mutate or delay the request headers.
  ///
  /// - parameter headers:   The current request headers.
  /// - parameter endStream: Whether this is a headers-only request.
  ///
  /// - returns: The header status containing headers with which to continue or buffer.
  func onRequestHeaders(_ headers: RequestHeaders, endStream: Bool)
    -> FilterHeadersStatus<RequestHeaders>

  /// Called any number of times whenever body data is sent.
  ///
  /// Filters may mutate or buffer (defer and concatenate) the data.
  ///
  /// - parameter body:      The outbound body data chunk.
  /// - parameter endStream: Whether this is the last data frame.
  ///
  /// - returns: The data status containing body with which to continue or buffer.
  func onRequestData(_ body: Data, endStream: Bool) -> FilterDataStatus

  /// Called at most once when the request is closed from the client with trailers.
  ///
  /// Filters may mutate or delay the trailers.
  ///
  /// - parameter trailers: The outbound trailers.
  ///
  /// - returns: The trailer status containing body with which to continue or buffer.
  func onRequestTrailers(_ trailers: RequestTrailers) -> FilterTrailersStatus<RequestTrailers>
}
