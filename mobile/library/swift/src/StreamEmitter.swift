import Foundation

/// Interface for a stream that may be canceled.
@objc
public protocol CancelableStream {
  /// Cancel and end the associated stream.
  func cancel()
}

/// Interface allowing for sending/emitting data on an Envoy stream.
@objc
public protocol StreamEmitter: CancelableStream {
  /// Send data over the associated stream.
  ///
  /// - parameter data: Data to send over the stream.
  ///
  /// - returns: The stream emitter, for chaining syntax.
  @discardableResult
  func sendData(_ data: Data) -> StreamEmitter

  /// Close the stream with trailers.
  ///
  /// - parameter trailers: Trailers with which to close the stream.
  func close(trailers: [String: [String]])

  /// Close the stream with a data frame.
  ///
  /// - parameter data: Data with which to close the stream.
  func close(data: Data)
}
