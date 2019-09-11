import Foundation

/// Client that is able to send and receive HTTP requests.
@objc
public protocol HTTPClient {
  /// Start a new stream.
  ///
  /// - parameter request: The request for opening a stream.
  /// - parameter handler: Handler for receiving stream events.
  ///
  /// - returns: Emitter for sending streaming data outward.
  func send(_ request: Request, handler: ResponseHandler) -> StreamEmitter

  /// Convenience function for sending a complete (non-streamed) request.
  ///
  /// - parameter request:  The request to send.
  /// - parameter body:     Serialized data to send as the body of the request.
  /// - parameter trailers: Trailers to send with the request.
  ///
  /// - returns: A cancelable request.
  @discardableResult
  func send(_ request: Request, body: Data?,
            trailers: [String: [String]], handler: ResponseHandler)
    -> CancelableStream
}
