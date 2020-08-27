@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy implementation of StatsClient.
final class StatsClientImpl: NSObject {
  private let engine: EnvoyEngine

  init(engine: EnvoyEngine) {
    self.engine = engine
    super.init()
  }
}

extension StatsClientImpl: StatsClient {
  func counter(elements: [Element]) -> Counter {
    return CounterImpl(elements: elements, engine: self.engine)
  }
}
