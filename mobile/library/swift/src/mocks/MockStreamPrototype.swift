import Dispatch
@_implementationOnly import EnvoyEngine
import Foundation

/// Mock implementation of `StreamPrototype` which is used to produce `MockStream` instances.
@objcMembers
public final class MockStreamPrototype: StreamPrototype {
  private let onStart: ((MockStream) -> Void)?

  /// Initialize a new instance of the mock prototype.
  ///
  /// - parameter onStart: Closure that will be called each time a new stream
  ///                      is started from the prototype.
  init(onStart: @escaping (MockStream) -> Void) {
    self.onStart = onStart
    super.init(engine: MockEnvoyEngine())
  }

  public override func start(queue: DispatchQueue = .main) -> Stream {
    let callbacks = self.createCallbacks(queue: queue)
    let stream = MockStream(mockStream: MockEnvoyHTTPStream(handle: 0, callbacks: callbacks))
    self.onStart?(stream)
    return stream
  }
}
