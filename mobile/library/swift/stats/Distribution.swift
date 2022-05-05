import Foundation

/// A time series distribution tracking quantile/sum/average stats.
@objc
public protocol Distribution: AnyObject {
  /// Record a new value to add to the integer distribution.
  ///
  /// - parameter value: The value to record.
  func recordValue(value: Int)

  /// Record a new value to add to the integer distribution with tags.
  ///
  /// - parameter tags:  The tags to attach to this distribution.
  /// - parameter value: The value to record.
  func recordValue(tags: Tags, value: Int)
}
