import Foundation

/// Engine represents a running instance of Envoy Mobile, and provides client interfaces that run on
/// that instance.
@objc
public protocol Engine: AnyObject {
  /// - returns: A client for opening and managing HTTP streams.
  func streamClient() -> StreamClient

  /// - returns: A client for recording time series metrics.
  func pulseClient() -> PulseClient

  func dumpStats() -> String

  /// Drains connections matching the given socket tag.
  func drainConnectionsBySocketTag(_ tag: UInt32)

  /// Terminates the running engine.
  func terminate()

  /// Refresh DNS, and drain connections owned by this Engine.
  func resetConnectivityState()
}
