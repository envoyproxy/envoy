@testable import Envoy
import EnvoyEngine
import Foundation

final class MockEnvoyEngine: NSObject {
  private let streamFactory: ((EnvoyHTTPCallbacks) -> EnvoyHTTPStream)?

  /// Closure called when `run(withConfig:)` is called.
  static var onRunWithConfig: ((_ config: EnvoyConfiguration, _ logLevel: String?) -> Void)?
  /// Closure called when `run(withConfigYAML:)` is called.
  static var onRunWithYAML: ((_ configYAML: String, _ logLevel: String?) -> Void)?

  init(streamFactory: ((EnvoyHTTPCallbacks) -> EnvoyHTTPStream)?) {
    self.streamFactory = streamFactory
    super.init()
  }

  override init() {
    self.streamFactory = nil
    super.init()
  }
}

extension MockEnvoyEngine: EnvoyEngine {
  func run(withConfig config: EnvoyConfiguration, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithConfig?(config, logLevel)
    return 0
  }

  func run(withConfigYAML configYAML: String, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithYAML?(configYAML, logLevel)
    return 0
  }

  func startStream(with callbacks: EnvoyHTTPCallbacks) -> EnvoyHTTPStream {
    if let factory = self.streamFactory {
      return factory(callbacks)
    } else {
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }
  }
}
