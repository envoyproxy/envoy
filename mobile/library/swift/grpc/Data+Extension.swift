import Foundation

extension Data {
  /// Gets the integer at the provided index using the size of `T`.
  /// Returns nil if the data is too small.
  ///
  /// - parameter index: The index at which to get the integer value.
  ///
  /// - returns: The next integer in the data, or nil.
  func integer<T: FixedWidthInteger>(atIndex index: Int) -> T? {
    let size = MemoryLayout<T>.size
    guard self.count >= index + size else {
      return nil
    }

    var value: T = 0
    _ = Swift.withUnsafeMutableBytes(of: &value) { valuePointer in
      self.copyBytes(to: valuePointer, from: index ..< index + size)
    }

    return value
  }
}
