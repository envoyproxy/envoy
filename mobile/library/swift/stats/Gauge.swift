import Foundation

/// A time series gauge.
@objc
public protocol Gauge: AnyObject {
  /// Set the gauge to the given value.
  ///
  /// - parameter value: Set the gauge to the given value.
  func set(value: Int)

  /// Set the gauge to the given value and tags.
  ///
  /// - parameter tags:  The tags to attach to this Gauge.
  /// - parameter value: Set the gauge to the given value.
  func set(tags: Tags, value: Int)

  /// Add the given amount to the gauge.
  ///
  /// - parameter amount: Add the given amount to the gauge.
  func add(amount: Int)

  /// Add the given amount to the gauge with tags.
  ///
  /// - parameter tags:   The tags to attach to this Gauge.
  /// - parameter amount: Add the given amount to the gauge.
  func add(tags: Tags, amount: Int)

  /// Subtract the given amount from the gauge.
  ///
  /// - parameter amount: Subtract the given amount from the gauge.
  func sub(amount: Int)

  /// Subtract the given amount from the gauge with tags.
  ///
  /// - parameter tags:   The tags to attach to this Gauge.
  /// - parameter amount: Subtract the given amount from the gauge.
  func sub(tags: Tags, amount: Int)
}
