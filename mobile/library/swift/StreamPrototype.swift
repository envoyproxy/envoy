import Dispatch
@_implementationOnly import EnvoyEngine
import Foundation

/// A type representing a stream that has not yet been started.
///
/// Constructed via `StreamClient`, and used to assign response callbacks
/// prior to starting an `Stream` by calling `start()`.
@objcMembers
public class StreamPrototype: NSObject {
  private let engine: EnvoyEngine
  private let callbacks = StreamCallbacks()
  private var explicitFlowControl = false

  /// Initialize a new instance of the stream prototype.
  ///
  /// - parameter engine: Engine to use for starting streams.
  init(engine: EnvoyEngine) {
    self.engine = engine
    super.init()
  }

  /// Create engine callbacks using the provided queue.
  ///
  /// - parameter queue: Queue on which to receive callback events.
  ///
  /// - returns: A new set of engine callbacks.
  func createCallbacks(queue: DispatchQueue) -> EnvoyHTTPCallbacks {
    return EnvoyHTTPCallbacks(callbacks: self.callbacks, queue: queue)
  }

  // MARK: - Public

  /// Start a new stream.
  ///
  /// - parameter queue: Queue on which to receive callback events.
  ///
  /// - returns: The new stream.
  public func start(queue: DispatchQueue = .main) -> Stream {
    let engineStream = self.engine.startStream(
      with: self.createCallbacks(queue: queue),
      explicitFlowControl: explicitFlowControl
    )
    return Stream(underlyingStream: engineStream)
  }

  /// Allows explicit flow control to be enabled. When explicit flow control is enabled, the owner
  /// of a stream is responsible for providing a buffer to receive response body data. If the buffer
  /// is smaller than the amount of data available, response callbacks will halt, and the underlying
  /// network protocol may signal for the server to stop sending data, until more space is
  /// available. This can limit the memory consumed by a server response, but may also result in
  /// reduced overall throughput, depending on usage.
  ///
  /// - parameter enabled: Whether explicit flow control will be enabled for the stream.
  /// - returns:  This stream, for chaining syntax.
  public func setExplicitFlowControl(enabled: Bool) -> StreamPrototype {
    self.explicitFlowControl = enabled
    return self
  }

  /// Specify a callback to be invoked when response headers are received by the stream.
  /// If `endStream` is `true`, the stream is complete, pending an onComplete callback.
  ///
  /// - parameter closure: Closure which will receive the headers
  ///                      and flag indicating if the stream is headers-only.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnResponseHeaders(
    closure: @escaping (_ headers: ResponseHeaders, _ endStream: Bool,
                        _ streamIntel: StreamIntel) -> Void
  ) -> StreamPrototype {
    self.callbacks.onHeaders = closure
    return self
  }

  /// Specify a callback to be invoked when a data frame is received by the stream.
  /// If `endStream` is `true`, the stream is complete, pending an onComplete callback.
  ///
  /// - parameter closure: Closure which will receive the data
  ///                      and flag indicating whether this is the last data frame.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnResponseData(
    closure: @escaping (_ body: Data, _ endStream: Bool, _ streamIntel: StreamIntel) -> Void
  ) -> StreamPrototype {
    self.callbacks.onData = closure
    return self
  }

  /// Specify a callback to be invoked when trailers are received by the stream.
  /// If the closure is called, the stream is complete, pending an onComplete callback.
  ///
  /// - parameter closure: Closure which will receive the trailers.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnResponseTrailers(
    closure: @escaping (_ trailers: ResponseTrailers, _ streamIntel: StreamIntel) -> Void
  ) -> StreamPrototype {
    self.callbacks.onTrailers = closure
    return self
  }

  /// Specify a callback to be invoked when additional send window becomes available.
  /// This is only ever called when the library is in explicit flow control mode. When enabled,
  /// the issuer should wait for this callback after calling sendData, before making another call
  /// to sendData.
  ///
  /// - parameter closure: Closure which will be called when additional send window becomes
  ///                      available.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnSendWindowAvailable(
    closure: @escaping (_ streamIntel: StreamIntel) -> Void
  ) -> StreamPrototype {
    callbacks.onSendWindowAvailable = closure
    return self
  }

  /// Specify a callback to be invoked when an internal Envoy exception occurs with the stream.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when an error occurs.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnError(
    closure: @escaping (_ error: EnvoyError, _ streamIntel: FinalStreamIntel) -> Void
  ) -> StreamPrototype {
    self.callbacks.onError = closure
    return self
  }

  /// Specify a callback to be invoked when the stream is canceled.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when the stream is canceled.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnCancel(
    closure: @escaping (_ streamIntel: FinalStreamIntel) -> Void
  ) -> StreamPrototype {
    self.callbacks.onCancel = closure
    return self
  }

  /// Specify a callback to be invoked when the stream completes gracefully.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when the stream is canceled.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnComplete(
    closure: @escaping (_ streamIntel: FinalStreamIntel) -> Void
  ) -> StreamPrototype {
    self.callbacks.onCancel = closure
    return self
  }
}
