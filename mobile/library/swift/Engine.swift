import Foundation

/// Engine represents a running instance of Envoy Mobile, and provides client interfaces that run on
/// that instance.
@objc
public protocol Engine: AnyObject {
  /// - returns: A client for opening and managing HTTP streams.
  func streamClient() -> StreamClient

  /// A client for recording time series metrics.
  func pulseClient() -> PulseClient

  /// Flush the stats sinks outside of a flushing interval.
  /// Note: stat flushing is done asynchronously, this function will never block.
  /// This is a noop if called before the underlying EnvoyEngine has started.
  func flushStats()

  /// Terminates the running engine.
  func terminate()

  /// Drain all connections owned by this Engine.
  func drainConnections()
}
