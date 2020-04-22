import Foundation

/// Client that is able to send and receive HTTP requests.
@objc
public protocol HTTPClient {
  /// Start a new stream.
  ///
  /// - parameter request: The request headers for opening a stream.
  /// - parameter handler: Handler for receiving stream events.
  ///
  /// - returns: Emitter for sending streaming data outward.
  func start(_ request: Request, handler: ResponseHandler) -> StreamEmitter

  /// Send a unary (non-streamed) request.
  ///
  /// Close with headers-only: Pass a nil body and nil trailers.
  /// Close with data:         Pass a body and nil trailers.
  /// Close with trailers:     Pass non-nil trailers.
  ///
  /// - parameter request:  The request headers to send.
  /// - parameter body:     Data to send as the body.
  /// - parameter trailers: Trailers to send with the request.
  /// - parameter handler:  Handler for receiving response events.
  ///
  /// - returns: A cancelable request.
  @discardableResult
  func send(_ request: Request, body: Data?,
            trailers: [String: [String]]?, handler: ResponseHandler)
    -> CancelableStream
}
