import Foundation

/// Callback interface for receiving stream events.
@objcMembers
public final class ResponseHandler: NSObject {
  /// Underlying observer which will be passed to the Envoy Engine.
  let underlyingObserver = EnvoyObserver()

  /// Initialize a new instance of the handler.
  ///
  /// - parameter queue: Dispatch queue upon which callbacks will be called.
  public init(queue: DispatchQueue = .main) {
    self.underlyingObserver.dispatchQueue = queue
  }

  /// Specify a callback for when response headers are received by the stream.
  /// If `endStream` is `true`, the stream is complete.
  ///
  /// - parameter closure: Closure which will receive the headers, status code,
  ///                      and flag indicating if the stream is headers-only.
  @discardableResult
  public func onHeaders(_ closure:
    @escaping (_ headers: [String: [String]], _ statusCode: Int, _ endStream: Bool) -> Void)
    -> ResponseHandler
  {
    self.underlyingObserver.onHeaders = { headers, endStream in
      closure(headers, ResponseHandler.statusCode(fromHeaders: headers), endStream)
    }

    return self
  }

  /// Specify a callback for when a data frame is received by the stream.
  /// If `endStream` is `true`, the stream is complete.
  ///
  /// - parameter closure: Closure which will receive the data,
  ///                      and flag indicating if the stream is complete.
  @discardableResult
  public func onData(_ closure:
    @escaping (_ data: Data, _ endStream: Bool) -> Void)
    -> ResponseHandler
  {
    self.underlyingObserver.onData = closure
    return self
  }

  /// Specify a callback for when trailers are received by the stream.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will receive the trailers.
  @discardableResult
  public func onTrailers(_ closure:
    @escaping (_ trailers: [String: [String]]) -> Void)
    -> ResponseHandler
  {
    self.underlyingObserver.onTrailers = closure
    return self
  }

  /// Specify a callback for when an internal Envoy exception occurs with the stream.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when an error occurs.
  @discardableResult
  public func onError(_ closure:
    @escaping () -> Void)
    -> ResponseHandler
  {
    self.underlyingObserver.onError = closure
    return self
  }

  /// Specify a callback for when the stream is canceled.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when the stream is canceled.
  @discardableResult
  public func onCancel(_ closure:
    @escaping () -> Void)
    -> ResponseHandler
  {
    self.underlyingObserver.onCancel = closure
    return self
  }

  // MARK: - Helpers

  /// Parses out the status code from the provided HTTP headers.
  ///
  /// - parameter headers: The headers from which to obtain the status.
  ///
  /// - returns: The HTTP status code from the headers, or 0 if none is set.
  static func statusCode(fromHeaders headers: [String: [String]]) -> Int {
    return headers[":status"]?
      .compactMap(Int.init)
      .first ?? 0
  }
}
