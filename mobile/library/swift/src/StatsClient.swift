import Foundation

/// Client used to record time series metrics.
///
/// Note: this an experimental interface and is subject to change. The implementation has not been
/// optimized, and there may be performance implications in production usage.
@objc
public protocol StatsClient: AnyObject {
  /// - parameter elements: Elements to identify a counter
  ///
  /// - returns: A Counter based on the joined elements.
  func counter(elements: [Element]) -> Counter

  /// - parameter elements: Elements to identify a gauge
  ///
  /// - returns: A Gauge based on the joined elements.
  func gauge(elements: [Element]) -> Gauge
}
