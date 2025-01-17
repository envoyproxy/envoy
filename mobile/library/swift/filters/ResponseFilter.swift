import Foundation

/// Filter executed for inbound responses, providing the ability to observe and mutate streams.
public protocol ResponseFilter: Filter {
  /// Called once when the response is initiated.
  ///
  /// Filters may mutate or delay the response headers.
  ///
  /// - parameter headers:     The current response headers.
  /// - parameter endStream:   Whether this is a headers-only response.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The header status containing headers with which to continue or buffer.
  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
    -> FilterHeadersStatus<ResponseHeaders>

  /// Called any number of times whenever body data is received.
  ///
  /// Filters may mutate or buffer (defer and concatenate) the data.
  ///
  /// - parameter body:        The inbound body data chunk.
  /// - parameter endStream:   Whether this is the last data frame.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The data status containing body with which to continue or buffer.
  func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
    -> FilterDataStatus<ResponseHeaders>

  /// Called at most once when the response is closed from the server with trailers.
  ///
  /// Filters may mutate or delay the trailers. Note trailers imply the stream has ended.
  ///
  /// - parameter trailers:    The outbound trailers.
  /// - parameter streamIntel: Internal HTTP stream metrics, context, and other details.
  ///
  /// - returns: The trailer status containing body with which to continue or buffer.
  func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
    -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers>

  /// Called at most once when an error within Envoy occurs.
  ///
  /// Only one of `onError`, `onCancel`, or `onComplete` will be called per stream.
  /// This should be considered a terminal state, and invalidates any previous attempts to
  /// `stopIteration{...}`.
  ///
  /// - parameter error:       The error that occurred within Envoy.
  /// - parameter streamIntel: Final internal HTTP stream metrics, context, and other details.
  func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel)

  /// Called at most once when the client cancels the stream.
  ///
  /// Only one of `onError`, `onCancel`, or `onComplete` will be called per stream.
  /// This should be considered a terminal state, and invalidates any previous attempts to
  /// `stopIteration{...}`.
  ///
  /// - parameter streamIntel: Final internal HTTP stream metrics, context, and other details.
  func onCancel(streamIntel: FinalStreamIntel)

  /// Called at most once when the stream completes gracefully.
  ///
  /// Only one of `onError`, `onCancel`, or `onComplete` will be called per stream.
  /// This should be considered a terminal state, and invalidates any previous attempts to
  /// `stopIteration{...}`.
  ///
  /// - parameter streamIntel: Final internal HTTP stream metrics, context, and other details.
  func onComplete(streamIntel: FinalStreamIntel)
}
