import Foundation

/// A time series distribution of duration measurements.
@objc
public protocol Timer: AnyObject {
  /// Record a new duration to add to the timer.
  ///
  /// - parameter durationMs: The duration to add in milliseconds.
  func recordDuration(durationMs: Int)

  /// Record a new duration to add to the timer along with tags.
  ///
  /// - parameter tags:       The tags to attach to this Timer.
  /// - parameter durationMs: The duration to add in milliseconds.
  func recordDuration(tags: Tags, durationMs: Int)
}
