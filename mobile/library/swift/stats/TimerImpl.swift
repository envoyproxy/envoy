@_implementationOnly import EnvoyEngine
import Foundation

/// The implementation of timer that can be used to track a distribution of time durations
@objcMembers
final class TimerImpl: NSObject, Timer {
  private let series: String
  private let tags: Tags
  private weak var engine: EnvoyEngine?

  init(elements: [Element], tags: Tags, engine: EnvoyEngine) {
    self.series = elements.map { $0.value }.joined(separator: ".")
    self.engine = engine
    self.tags = tags
    super.init()
  }

  /// Record a new duration value for the distribution.
  ///
  /// - parameter durationMs: The duration to record in milliseconds.
  func recordDuration(durationMs: Int) {
    // TODO(jingwei99) potentially surface error up if engine is nil.
    self.engine?.recordHistogramDuration(
      self.series, tags: self.tags.allTags(), durationMs: numericCast(durationMs))
  }

  /// Record a new duration value for the distribution with tags.
  ///
  /// - parameter tags:       The tags to attach to this timer.
  /// - parameter durationMs: The duration to record in milliseconds.
  func recordDuration(tags: Tags, durationMs: Int) {
    self.engine?.recordHistogramDuration(
      self.series, tags: tags.allTags(), durationMs: numericCast(durationMs))
  }
}
