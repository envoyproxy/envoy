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
    return CounterImpl(elements: elements, tags: TagsBuilder().build(), engine: self.engine)
  }

  func counter(elements: [Element], tags: Tags) -> Counter {
    return CounterImpl(elements: elements, tags: tags, engine: self.engine)
  }

  func gauge(elements: [Element]) -> Gauge {
    return GaugeImpl(elements: elements, tags: TagsBuilder().build(), engine: self.engine)
  }

  func gauge(elements: [Element], tags: Tags) -> Gauge {
    return GaugeImpl(elements: elements, tags: tags, engine: self.engine)
  }

  func timer(elements: [Element]) -> Timer {
    return TimerImpl(elements: elements, tags: TagsBuilder().build(), engine: self.engine)
  }

  func timer(elements: [Element], tags: Tags) -> Timer {
    return TimerImpl(elements: elements, tags: tags, engine: self.engine)
  }

  func distribution(elements: [Element], tags: Tags) -> Distribution {
    return DistributionImpl(elements: elements, tags: tags, engine: self.engine)
  }

  func distribution(elements: [Element]) -> Distribution {
    return DistributionImpl(elements: elements, tags: TagsBuilder().build(), engine: self.engine)
  }
}
