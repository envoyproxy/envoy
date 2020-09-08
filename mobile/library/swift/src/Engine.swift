import Foundation

/// Engine represents a running instance of Envoy Mobile, and provides client interfaces that run on
/// that instance.
@objc
public protocol Engine: AnyObject {
  /// - returns: A client for opening and managing HTTP streams.
  func streamClient() -> StreamClient

  /// A client for recording time series metrics.
  func statsClient() -> StatsClient
}
