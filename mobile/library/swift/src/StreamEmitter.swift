import Foundation

/// Interface allowing for sending/emitting data on an Envoy stream.
@objc
public protocol StreamEmitter {
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

  /// End the stream after sending any provided trailers.
  ///
  /// - parameter trailers: Trailers to send over the stream.
  func close(trailers: [String: [String]])

  /// Cancel and end the associated stream.
  func cancel()
}

extension StreamEmitter {
  /// Convenience function for ending the stream without sending any trailers.
  public func close() {
    self.close(trailers: [:])
  }
}
