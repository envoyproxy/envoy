@_implementationOnly import EnvoyEngine
import Foundation

/// Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
final class MockEnvoyEngine: NSObject {
  init(runningCallback onEngineRunning: (() -> Void)? = nil,
       logger: ((Int, String) -> Void)? = nil,
       eventTracker: (([String: String]) -> Void)? = nil, networkMonitoringMode: Int32 = 0) {}

  /// Closure called when `run(withConfig:)` is called.
  static var onRunWithConfig: ((_ config: EnvoyConfiguration, _ logLevel: String?) -> Void)?

  /// Closure called when `recordCounterInc(_:tags:count:)` is called.
  static var onRecordCounter: (
    (_ elements: String, _ tags: [String: String], _ count: UInt) -> Void)?
}

extension MockEnvoyEngine: EnvoyEngine {
  func run(withConfig config: EnvoyConfiguration, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithConfig?(config, logLevel)
    return kEnvoySuccess
  }

  func startStream(
    with callbacks: EnvoyHTTPCallbacks,
    explicitFlowControl: Bool
  ) -> EnvoyHTTPStream {
    return MockEnvoyHTTPStream(handle: 0, engine: 0, callbacks: callbacks,
                               explicitFlowControl: explicitFlowControl)
  }

  func recordCounterInc(_ elements: String, tags: [String: String], count: UInt) -> Int32 {
    MockEnvoyEngine.onRecordCounter?(elements, tags, count)
    return kEnvoySuccess
  }

  func dumpStats() -> String {
    return ""
  }

  func terminate() {}

  func resetConnectivityState() {}
}
