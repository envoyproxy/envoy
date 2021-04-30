import Foundation

/// A time series distribution of duration measurements.
@objc
public protocol Timer: AnyObject {
  /// Record a new duration to add to the timer.
  func completeWithDuration(durationMs: Int)

  /// Record a new duration to add to the timer along with tags.
  func completeWithDuration(tags: Tags, durationMs: Int)
}
