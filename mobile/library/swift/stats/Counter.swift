import Foundation

/// A time series counter.
@objc
public protocol Counter: AnyObject {
  /// Increment the counter by the given count.
  ///
  /// - parameter count: The count to add to this counter.
  func increment(count: Int)

  /// Increment the counter by the given count and along with tags.
  ///
  /// - parameter tags:  The tags to attach to this Counter.
  /// - parameter count: The count to add to this counter.
  func increment(tags: Tags, count: Int)
}

extension Counter {
  /// Increment the counter by 1.
  public func increment() {
    self.increment(count: 1)
  }
}
