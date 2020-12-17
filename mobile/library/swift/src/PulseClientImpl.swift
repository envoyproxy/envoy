@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy implementation of PulseClient.
final class PulseClientImpl: NSObject {
  private let engine: EnvoyEngine

  init(engine: EnvoyEngine) {
    self.engine = engine
    super.init()
  }
}

extension PulseClientImpl: PulseClient {
  func counter(elements: [Element]) -> Counter {
    return CounterImpl(elements: elements, engine: self.engine)
  }

  func gauge(elements: [Element]) -> Gauge {
    return GaugeImpl(elements: elements, engine: self.engine)
  }
}
