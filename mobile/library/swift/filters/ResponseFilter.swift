import Foundation

/// Filter executed for inbound responses, providing the ability to observe and mutate streams.
public protocol ResponseFilter: Filter {
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
  func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus<ResponseHeaders>

  /// Called at most once when the response is closed from the server with trailers.
  ///
  /// Filters may mutate or delay the trailers. Note trailers imply the stream has ended.
  ///
  /// - parameter trailers: The outbound trailers.
  ///
  /// - returns: The trailer status containing body with which to continue or buffer.
  func onResponseTrailers(_ trailers: ResponseTrailers)
    -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers>

  /// Called at most once when an error within Envoy occurs.
  ///
  /// This should be considered a terminal state, and invalidates any previous attempts to
  /// `stopIteration{...}`.
  ///
  /// - parameter error: The error that occurred within Envoy.
  func onError(_ error: EnvoyError)

  /// Called at most once when the client cancels the stream.
  ///
  /// This should be considered a terminal state, and invalidates any previous attempts to
  /// `stopIteration{...}`.
  func onCancel()
}
