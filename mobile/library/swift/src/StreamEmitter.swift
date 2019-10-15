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

  /// Send metadata over the associated stream.
  ///
  /// - parameter metadata: Metadata to send over the stream.
  ///
  /// - returns: The stream emitter, for chaining syntax.
  @discardableResult
  func sendMetadata(_ metadata: [String: [String]]) -> StreamEmitter

  /// End the stream.
  ///
  /// - parameter trailers: Trailers with which to close the stream.
  //                        If nil, stream will be closed with an empty data frame.
  func close(trailers: [String: [String]]?)
}
