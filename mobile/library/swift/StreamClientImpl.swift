@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy implementation of StreamClient.
final class StreamClientImpl: NSObject {
  private let engine: EnvoyEngine

  init(engine: EnvoyEngine) {
    self.engine = engine
    super.init()
  }
}

extension StreamClientImpl: StreamClient {
  func newStreamPrototype() -> StreamPrototype {
    return StreamPrototype(engine: self.engine)
  }
}
