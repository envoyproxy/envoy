import Foundation

/// A time series distribution of duration measurements.
@objc
public protocol Timer: AnyObject {
  /// Record a new duration to add to the timer.
  func recordDuration(durationMs: Int)

  /// Record a new duration to add to the timer along with tags.
  func recordDuration(tags: Tags, durationMs: Int)
}
