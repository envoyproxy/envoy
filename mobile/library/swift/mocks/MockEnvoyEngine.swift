@_implementationOnly import EnvoyEngine
import Foundation

/// Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
final class MockEnvoyEngine: NSObject {
  init(runningCallback onEngineRunning: (() -> Void)? = nil, logger: ((String) -> Void)? = nil,
       eventTracker: (([String: String]) -> Void)? = nil, networkMonitoringMode: Int32 = 0) {}

  /// Closure called when `run(withConfig:)` is called.
  static var onRunWithConfig: ((_ config: EnvoyConfiguration, _ logLevel: String?) -> Void)?
  /// Closure called when `run(withConfigYAML:)` is called.
  static var onRunWithTemplate: ((
    _ template: String,
    _ config: EnvoyConfiguration,
    _ logLevel: String?
  ) -> Void)?

  /// Closure called when `recordCounterInc(_:tags:count:)` is called.
  static var onRecordCounter: (
    (_ elements: String, _ tags: [String: String], _ count: UInt) -> Void)?
  /// Closure called when `recordGaugeSet(_:value:)` is called.
  static var onRecordGaugeSet: (
    (_ elements: String, _ tags: [String: String], _ value: UInt) -> Void)?
  /// Closure called when `recordGaugeAdd(_:amount:)` is called.
  static var onRecordGaugeAdd: (
    (_ elements: String, _ tags: [String: String], _ amount: UInt) -> Void)?
  /// Closure called when `recordGaugeSub(_:amount:)` is called.
  static var onRecordGaugeSub: (
    (_ elements: String, _ tags: [String: String], _ amount: UInt) -> Void)?
  /// Closure called when `recordHistogramDuration(_:durationMs)` is called.
  static var onRecordHistogramDuration: (
    (_ elements: String, _ tags: [String: String], _ durationMs: UInt) -> Void)?
  /// Closure called when `recordHistogramValue(_:value)` is called.
  static var onRecordHistogramValue: (
    (_ elements: String, _ tags: [String: String], _ value: UInt) -> Void)?
  static var onFlushStats: (() -> Void)?
}

extension MockEnvoyEngine: EnvoyEngine {
  func run(withConfig config: EnvoyConfiguration, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithConfig?(config, logLevel)
    return kEnvoySuccess
  }

  func run(withTemplate template: String, config: EnvoyConfiguration, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithTemplate?(template, config, logLevel)
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

  func recordGaugeSet(_ elements: String, tags: [String: String], value: UInt) -> Int32 {
    MockEnvoyEngine.onRecordGaugeSet?(elements, tags, value)
    return kEnvoySuccess
  }

  func recordGaugeAdd(_ elements: String, tags: [String: String], amount: UInt) -> Int32 {
    MockEnvoyEngine.onRecordGaugeAdd?(elements, tags, amount)
    return kEnvoySuccess
  }

  func recordGaugeSub(_ elements: String, tags: [String: String], amount: UInt) -> Int32 {
    MockEnvoyEngine.onRecordGaugeSub?(elements, tags, amount)
    return kEnvoySuccess
  }

  func recordHistogramDuration(
    _ elements: String, tags: [String: String], durationMs: UInt) -> Int32 {
    MockEnvoyEngine.onRecordHistogramDuration?(elements, tags, durationMs)
    return kEnvoySuccess
  }

  func recordHistogramValue(_ elements: String, tags: [String: String], value: UInt) -> Int32 {
    MockEnvoyEngine.onRecordHistogramValue?(elements, tags, value)
    return kEnvoySuccess
  }

  func flushStats() {
    MockEnvoyEngine.onFlushStats?()
  }

  func dumpStats() -> String {
    return ""
  }

  func terminate() {}

  func resetConnectivityState() {}
}
