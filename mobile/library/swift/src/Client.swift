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
  func send(_ request: Request, handler: ResponseHandler) -> StreamEmitter
}

extension Client {
  /// Convenience function for sending a complete (non-streamed) request.
  ///
  /// - parameter request:  The request to send.
  /// - parameter data:     Serialized data to send as the body of the request.
  /// - parameter trailers: Trailers to send with the request.
  ///
  /// - returns: A cancelable request.
  @discardableResult
  public func send(_ request: Request, data: Data?,
                   trailers: [String: [String]] = [:], handler: ResponseHandler)
    -> CancelableStream
  {
    let emitter = self.send(request, handler: handler)
    if let data = data {
      emitter.sendData(data)
    }

    emitter.close(trailers: trailers)
    return emitter
  }
}
