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
}
