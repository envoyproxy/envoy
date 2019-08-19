import Foundation

/// Client that is able to send and receive requests through Envoy.
@objc
public protocol Client {
  /// Start a new stream.
  ///
  /// - parameter request: The request for opening a stream.
  /// - parameter handler: Handler for receiving stream events.
  ///
  /// - returns: Emitter for sending streaming data outward.
  func startStream(with request: Request, handler: ResponseHandler) -> StreamEmitter

  /// Convenience function for sending a unary request.
  ///
  /// - parameter request:  The request to send.
  /// - parameter body:     Serialized data to send as the body of the request.
  /// - parameter trailers: Trailers to send with the request.
  func sendUnary(_ request: Request, body: Data?,
                 trailers: [String: [String]], handler: ResponseHandler)
}

extension Client {
  /// Convenience function for sending a unary request without trailers.
  ///
  /// - parameter request:  The request to send.
  /// - parameter body:     Serialized data to send as the body of the request.
  public func sendUnary(_ request: Request, body: Data?, handler: ResponseHandler) {
    self.sendUnary(request, body: body, trailers: [:], handler: handler)
  }
}
