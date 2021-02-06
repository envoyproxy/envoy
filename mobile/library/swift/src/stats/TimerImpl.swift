@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of timer that can be used to track a distribution of time durations
@objcMembers
class TimerImpl: NSObject, Timer {
  private let series: String
  private weak var engine: EnvoyEngine?

  init(elements: [Element], engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    super.init()
  }

  /// Record a new duration value for the distribution.
  /// TODO: potentially raise error to platform if the operation is not successful.
  func completeWithDuration(durationMs: Int) {
    guard let engine = self.engine else {
      return
    }

    engine.recordHistogramDuration(self.series, durationMs: numericCast(durationMs))
  }
}
