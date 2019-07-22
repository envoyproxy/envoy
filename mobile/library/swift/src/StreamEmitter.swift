import Foundation

/// Interface allowing for sending/emitting data on an Envoy stream.
@objc
public protocol StreamEmitter {
  /// Returns whether the emitter is still active and can
  /// perform communications with the associated stream.
  ///
  /// - returns: True if the stream is active.
  func isActive() -> Bool

  /// Send data over the associated stream.
  ///
  /// - parameter data: Data to send over the stream.
  ///
  /// - throws: `Envoy.Error` when the stream is inactive or data can't be sent.
  ///
  /// - returns: The stream emitter, for chaining syntax.
  func sendData(_ data: Data) throws -> StreamEmitter

  /// Sent metadata over the associated stream.
  ///
  /// - parameter metadata: Metadata to send over the stream.
  ///
  /// - throws: `Envoy.Error` when the stream is inactive or data can't be sent.
  ///
  /// - returns: The stream emitter, for chaining syntax.
  func sendMetadata(_ metadata: [String: [String]]) throws -> StreamEmitter

  /// End the stream after sending any provided trailers.
  ///
  /// - parameter trailers: Trailers to send over the stream.
  ///
  /// - throws: `Envoy.Error` when the stream is inactive or data can't be sent.
  func close(trailers: [String: [String]]) throws

  /// Cancel and end the associated stream.
  ///
  /// - throws: `Envoy.Error` when the stream is inactive or data can't be sent.
  func cancel() throws
}

extension StreamEmitter {
  /// Convenience function for ending the stream without sending any trailers.
  ///
  /// - throws: `Envoy.Error` when the stream is inactive or data can't be sent.
  public func close() throws {
    try self.close(trailers: [:])
  }
}
