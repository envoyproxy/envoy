@_implementationOnly import EnvoyEngine
import Foundation

/// Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
final class MockEnvoyEngine: NSObject {
  /// Closure called when `run(withConfig:)` is called.
  static var onRunWithConfig: ((_ config: EnvoyConfiguration, _ logLevel: String?) -> Void)?
  /// Closure called when `run(withConfigYAML:)` is called.
  static var onRunWithTemplate: ((
    _ template: String,
    _ config: EnvoyConfiguration,
    _ logLevel: String?
  ) -> Void)?
  /// Closure called when `recordCounterInc(_:count:)` is called.
  static var onRecordCounter: ((_ elements: String, _ count: UInt) -> Void)?
  /// Closure called when `recordGaugeSet(_:value:)` is called.
  static var onRecordGaugeSet: ((_ elements: String, _ value: UInt) -> Void)?
  /// Closure called when `recordGaugeAdd(_:amount:)` is called.
  static var onRecordGaugeAdd: ((_ elements: String, _ amount: UInt) -> Void)?
  /// Closure called when `recordGaugeSub(_:amount:)` is called.
  static var onRecordGaugeSub: ((_ elements: String, _ amount: UInt) -> Void)?
  /// Closure called when `recordHistogramDuration(_:durationMs)` is called.
  static var onRecordHistogramDuration: ((_ elements: String, _ durationMs: UInt) -> Void)?
  /// Closure called when `recordHistogramValue(_:value)` is called.
  static var onRecordHistogramValue: ((_ elements: String, _ value: UInt) -> Void)?
}

extension MockEnvoyEngine: EnvoyEngine {
  func run(withConfig config: EnvoyConfiguration, logLevel: String,
           onEngineRunning: (() -> Void)?, logger: ((String) -> Void)?) -> Int32
  {
    MockEnvoyEngine.onRunWithConfig?(config, logLevel)
    return kEnvoySuccess
  }

  func run(withTemplate template: String, config: EnvoyConfiguration, logLevel: String,
           onEngineRunning: (() -> Void)?, logger: ((String) -> Void)?) -> Int32
  {
    MockEnvoyEngine.onRunWithTemplate?(template, config, logLevel)
    return kEnvoySuccess
  }

  func startStream(with callbacks: EnvoyHTTPCallbacks) -> EnvoyHTTPStream {
    return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
  }

  func recordCounterInc(_ elements: String, count: UInt) -> Int32 {
    MockEnvoyEngine.onRecordCounter?(elements, count)
    return kEnvoySuccess
  }

  func recordGaugeSet(_ elements: String, value: UInt) -> Int32 {
    MockEnvoyEngine.onRecordGaugeSet?(elements, value)
    return kEnvoySuccess
  }

  func recordGaugeAdd(_ elements: String, amount: UInt) -> Int32 {
    MockEnvoyEngine.onRecordGaugeAdd?(elements, amount)
    return kEnvoySuccess
  }

  func recordGaugeSub(_ elements: String, amount: UInt) -> Int32 {
    MockEnvoyEngine.onRecordGaugeSub?(elements, amount)
    return kEnvoySuccess
  }

  func recordHistogramDuration(_ elements: String, durationMs: UInt) -> Int32 {
    MockEnvoyEngine.onRecordHistogramDuration?(elements, durationMs)
    return kEnvoySuccess
  }

  func recordHistogramValue(_ elements: String, value: UInt) -> Int32 {
    MockEnvoyEngine.onRecordHistogramValue?(elements, value)
    return kEnvoySuccess
  }
}
