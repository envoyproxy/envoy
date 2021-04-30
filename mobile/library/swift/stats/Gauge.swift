import Foundation

/// A time series gauge.
@objc
public protocol Gauge: AnyObject {
  /// Set the gauge with the given value.
  func set(value: Int)

  /// Set the gauge with the given value and tags.
  func set(tags: Tags, value: Int)

  /// Add the given amount to the gauge.
  func add(amount: Int)

  /// Add the given amount to the gauge with tags.
  func add(tags: Tags, amount: Int)

  /// Subtract the given amount from the gauge.
  func sub(amount: Int)

  /// Subtract the given amount from the gauge with tags.
  func sub(tags: Tags, amount: Int)
}
