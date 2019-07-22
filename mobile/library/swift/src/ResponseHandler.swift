import Foundation

/// Callback interface for receiving stream events.
@objc
public protocol ResponseHandler {
  /// Called when response headers are received by the stream.
  ///
  /// - parameter headers:    The headers of the response.
  /// - parameter statusCode: The status code of the response.
  func onHeaders(_ headers: [String: [String]], statusCode: Int)

  /// Called when a data frame is received by the stream.
  ///
  /// - parameter data:      Bytes in the response.
  /// - parameter endStream: True if the stream is complete.
  func onData(_ data: Data, endStream: Bool)

  /// Called when response metadata is received by the stream.
  ///
  /// - parameter metadata:  The metadata of the response.
  /// - parameter endStream: True if the stream is complete.
  func onMetadata(_ metadata: [String: [String]], endStream: Bool)

  /// Called when response trailers are received by the stream.
  ///
  /// - parameter trailers: The trailers of the response.
  func onTrailers(_ trailers: [String: [String]])

  /// Called when an internal Envoy exception occurs with the stream.
  ///
  /// - parameter error: The error that occurred with the stream.
  func onError(_ error: Error)

  /// Called when the stream is canceled.
  func onCanceled()

  /// Called when the stream has been completed.
  func onCompletion()
}
