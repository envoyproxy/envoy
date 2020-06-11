import Foundation

/// Mock implementation of `StreamClient` which produces `MockStreamPrototype` values.
@objcMembers
public final class MockStreamClient: NSObject {
  /// Closure that may be set to observe the creation of new streams.
  /// It will be called each time `newStreamPrototype()` is executed.
  ///
  /// Typically, this is used to capture streams on creation before sending values through them.
  public var onStartStream: ((MockStream) -> Void)?

  /// Initialize a new instance of the stream client.
  ///
  /// - parameter onStartStream: Closure that may be set to observe the creation of new streams.
  public init(onStartStream: ((MockStream) -> Void)? = nil) {
    self.onStartStream = onStartStream
    super.init()
  }
}

extension MockStreamClient: StreamClient {
  public func newStreamPrototype() -> StreamPrototype {
    return MockStreamPrototype(onStart: { [weak self] in self?.onStartStream?($0) })
  }
}
