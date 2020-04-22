@testable import Envoy
import Foundation

final class MockEnvoyEngine: NSObject {
  static var onRunWithConfig: ((_ config: EnvoyConfiguration, _ logLevel: String?) -> Void)?
  static var onRunWithYAML: ((_ configYAML: String, _ logLevel: String?) -> Void)?
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
    return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
  }
}
