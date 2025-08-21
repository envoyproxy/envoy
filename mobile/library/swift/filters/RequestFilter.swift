import Foundation

/// Filter executed for outbound requests, providing the ability to observe and mutate streams.
public protocol RequestFilter: Filter {
  /// Called once when the request is initiated.
  ///
  /// Filters may mutate or delay the request headers.
  ///
  /// - parameter headers:     The current request headers.
  /// - parameter endStream:   Whether this is a headers-only request.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The header status containing headers with which to continue or buffer.
  func onRequestHeaders(_ headers: RequestHeaders, endStream: Bool, streamIntel: StreamIntel)
    -> FilterHeadersStatus<RequestHeaders>

  /// Called any number of times whenever body data is sent.
  ///
  /// Filters may mutate or buffer (defer and concatenate) the data.
  ///
  /// - parameter body:        The outbound body data chunk.
  /// - parameter endStream:   Whether this is the last data frame.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The data status containing body with which to continue or buffer.
  func onRequestData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
    -> FilterDataStatus<RequestHeaders>

  /// Called at most once when the request is closed from the client with trailers.
  ///
  /// Filters may mutate or delay the trailers. Note trailers imply the stream has ended.
  ///
  /// - parameter trailers:    The outbound trailers.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The trailer status containing body with which to continue or buffer.
  func onRequestTrailers(_ trailers: RequestTrailers, streamIntel: StreamIntel)
    -> FilterTrailersStatus<RequestHeaders, RequestTrailers>
}
