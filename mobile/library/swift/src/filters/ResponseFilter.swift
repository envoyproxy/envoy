import Foundation

/// Filter executed for inbound responses, providing the ability to observe and mutate streams.
public protocol ResponseFilter: Filter {
  /// Called by the filter manager once to initialize the filter callbacks that the filter should
  /// use.
  ///
  /// - parameter callbacks: The callbacks for this filter to use to interact with the chain.
  func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks)

  /// Called once when the response is initiated.
  ///
  /// Filters may mutate or delay the response headers.
  ///
  /// - parameter headers:   The current response headers.
  /// - parameter endStream: Whether this is a headers-only response.
  ///
  /// - returns: The header status containing headers with which to continue or buffer.
  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool)
    -> FilterHeadersStatus<ResponseHeaders>

  /// Called any number of times whenever body data is received.
  ///
  /// Filters may mutate or buffer (defer and concatenate) the data.
  ///
  /// - parameter body:      The inbound body data chunk.
  /// - parameter endStream: Whether this is the last data frame.
  ///
  /// - returns: The data status containing body with which to continue or buffer.
  func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus

  /// Called at most once when the response is closed from the server with trailers.
  ///
  /// Filters may mutate or delay the trailers.
  ///
  /// - parameter trailers: The outbound trailers.
  ///
  /// - returns: The trailer status containing body with which to continue or buffer.
  func onResponseTrailers(_ trailers: ResponseTrailers) -> FilterTrailersStatus<ResponseTrailers>
}
