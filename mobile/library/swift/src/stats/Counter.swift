import Foundation

/// A time series counter.
@objc
public protocol Counter: AnyObject {
  /// Increment the counter by the given count.
  func increment(count: Int)
}

extension Counter {
  /// Increment the counter by 1.
  public func increment() {
    self.increment(count: 1)
  }
}
