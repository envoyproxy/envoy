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
  func startStream(request: Request, handler: ResponseHandler)
    -> StreamEmitter
}
