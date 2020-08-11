import Foundation

/// Engine represents a running instance of Envoy Mobile, and provides client interfaces that run on
/// that instance.
@objc
public protocol Engine: AnyObject {
  /// A client for opening and managing HTTP streams
  var streamClient: StreamClient { get }

  /// A client for recording time series metrics.
  var statsClient: StatsClient { get }
}
